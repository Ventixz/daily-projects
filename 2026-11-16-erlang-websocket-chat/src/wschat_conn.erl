%% One process per TCP connection: read an HTTP/1.1 request, route it, and
%% either answer and close, or upgrade to WebSocket and stay.
%%
%% Parsing uses the VM's built-in HTTP packet mode ({packet, http_bin}),
%% which splits the request line and headers for us. After the headers we
%% switch the same socket to {packet, raw} for the body or WebSocket frames.
-module(wschat_conn).
-export([serve/1]).

-define(READ_TIMEOUT, 10000).
-define(MAX_HEADERS, 64).
-define(MAX_BODY, 16384).
-define(MAX_WS_MESSAGE, 65536).

serve(Sock) ->
    ok = inet:setopts(Sock, [{packet, http_bin}, {packet_size, 8192}]),
    case read_request(Sock) of
        {ok, Req} -> route(Sock, Req);
        {error, Status} -> respond(Sock, Status, #{error => bad_request});
        closed -> ok
    end,
    gen_tcp:close(Sock).

%% ---- request reading ----------------------------------------------------

read_request(Sock) ->
    case gen_tcp:recv(Sock, 0, ?READ_TIMEOUT) of
        {ok, {http_request, Method, {abs_path, Target}, {1, _}}} ->
            read_headers(Sock, #{method => Method, target => Target}, #{}, 0);
        {ok, {http_request, _, _, _}} -> {error, 400};
        {ok, {http_error, _}} -> {error, 400};
        {error, emsgsize} -> {error, 414};
        {error, _} -> closed
    end.

read_headers(_Sock, _Req, _H, N) when N > ?MAX_HEADERS -> {error, 431};
read_headers(Sock, Req, H, N) ->
    case gen_tcp:recv(Sock, 0, ?READ_TIMEOUT) of
        {ok, {http_header, _, Name, _, Value}} ->
            %% Well-known names come back as atoms ('Content-Length'),
            %% others as binaries; normalise both to lower-case binaries.
            K = string:lowercase(if is_atom(Name) -> atom_to_binary(Name); true -> Name end),
            read_headers(Sock, Req, H#{K => Value}, N + 1);
        {ok, http_eoh} -> read_body(Sock, Req#{headers => H});
        {ok, {http_error, _}} -> {error, 400};
        {error, emsgsize} -> {error, 431};
        {error, _} -> closed
    end.

read_body(Sock, Req = #{headers := H}) ->
    ok = inet:setopts(Sock, [{packet, raw}]),
    case maps:get(<<"content-length">>, H, <<"0">>) of
        _ when is_map_key(<<"transfer-encoding">>, H) -> {error, 501};
        CL ->
            case catch binary_to_integer(CL) of
                0 -> {ok, Req#{body => <<>>}};
                Len when is_integer(Len), Len > 0, Len =< ?MAX_BODY ->
                    case gen_tcp:recv(Sock, Len, ?READ_TIMEOUT) of
                        {ok, Body} -> {ok, Req#{body => Body}};
                        {error, _} -> closed
                    end;
                Len when is_integer(Len), Len > ?MAX_BODY -> {error, 413};
                _ -> {error, 400}
            end
    end.

%% ---- routing ------------------------------------------------------------

route(Sock, Req = #{method := M, target := Target}) ->
    #{path := Path} = U = uri_string:parse(Target),
    Query = case U of #{query := Q} -> uri_string:dissect_query(Q); _ -> [] end,
    case {M, Path} of
        {'GET', <<"/">>} -> static(Sock, "index.html", <<"text/html; charset=utf-8">>);
        {'GET', <<"/chat.js">>} -> static(Sock, "chat.js", <<"text/javascript; charset=utf-8">>);
        {'GET', <<"/chat.css">>} -> static(Sock, "chat.css", <<"text/css; charset=utf-8">>);
        {'GET', <<"/ws">>} -> websocket(Sock, Req, Query);
        {'POST', <<"/poll/join">>} -> poll_join(Sock, Req);
        {'GET', <<"/poll/recv">>} -> with_session(Sock, Req, fun poll_recv/3);
        {'POST', <<"/poll/send">>} -> with_session(Sock, Req, fun poll_send/3);
        {'POST', <<"/poll/leave">>} -> with_session(Sock, Req, fun poll_leave/3);
        {_, P} when P =:= <<"/">>; P =:= <<"/chat.js">>; P =:= <<"/chat.css">>; P =:= <<"/ws">>;
                    P =:= <<"/poll/join">>; P =:= <<"/poll/recv">>;
                    P =:= <<"/poll/send">>; P =:= <<"/poll/leave">> ->
            respond(Sock, 405, #{error => method_not_allowed});
        _ -> respond(Sock, 404, #{error => not_found})
    end.

static(Sock, File, Type) ->
    %% Locate priv/ next to ebin/ (code:priv_dir needs a conventional app
    %% directory name, which a dated project folder isn't).
    Root = filename:dirname(filename:dirname(code:which(?MODULE))),
    {ok, Body} = file:read_file(filename:join([Root, "priv", File])),
    send_response(Sock, 200, [{<<"content-type">>, Type},
                              {<<"content-security-policy">>,
                               <<"default-src 'self'; connect-src 'self' ws: wss:">>},
                              {<<"x-content-type-options">>, <<"nosniff">>}], Body).

%% ---- long-poll fallback -------------------------------------------------
%% The session id travels in a custom header rather than the URL: it stays
%% out of access logs, and a custom header forces a CORS preflight on any
%% cross-site fetch -- which this server never approves.

poll_join(Sock, #{body := Nick}) ->
    case supervisor:start_child(wschat_poll_sup, [string:trim(Nick)]) of
        {ok, _Pid, {Sid, History}} ->
            respond(Sock, 200, #{sid => Sid, nick => string:trim(Nick),
                                 history => History,
                                 members => wschat_room:members()});
        {error, Reason} when is_atom(Reason) -> respond(Sock, error_status(Reason), #{error => Reason});
        {error, _Crash} -> respond(Sock, 500, #{error => internal})
    end.

with_session(Sock, #{headers := H} = Req, Fun) ->
    case wschat_poll_sup:lookup(maps:get(<<"x-chat-session">>, H, undefined)) of
        {ok, Pid} ->
            try Fun(Sock, Req, Pid)
            catch exit:{noproc, _} -> respond(Sock, 404, #{error => no_session});
                  exit:{normal, _} -> respond(Sock, 404, #{error => no_session})
            end;
        error -> respond(Sock, 404, #{error => no_session})
    end.

poll_recv(Sock, _Req, Pid) ->
    {ok, Events} = wschat_poll_session:recv(Pid),
    respond(Sock, 200, #{events => Events}).

poll_send(Sock, #{body := Text}, Pid) ->
    case wschat_poll_session:input(Pid, Text) of
        ok -> respond(Sock, 204, none);
        {error, Reason} -> respond(Sock, error_status(Reason), #{error => Reason})
    end.

poll_leave(Sock, _Req, Pid) ->
    ok = wschat_poll_session:stop(Pid),
    respond(Sock, 204, none).

error_status(nick_taken) -> 409;
error_status(_) -> 400.

%% ---- WebSocket ----------------------------------------------------------

websocket(Sock, #{headers := H}, Query) ->
    Key = maps:get(<<"sec-websocket-key">>, H, <<>>),
    Checks = [has_token(maps:get(<<"upgrade">>, H, <<>>), <<"websocket">>),
              has_token(maps:get(<<"connection">>, H, <<>>), <<"upgrade">>),
              valid_key(Key)],
    Nick = proplists:get_value(<<"nick">>, Query, <<>>),
    case {lists:all(fun(X) -> X end, Checks),
          maps:get(<<"sec-websocket-version">>, H, <<>>), same_origin(H)} of
        {false, _, _} -> respond(Sock, 400, #{error => not_a_websocket_handshake});
        {true, V, _} when V =/= <<"13">> ->
            send_response(Sock, 426, [{<<"sec-websocket-version">>, <<"13">>}], <<>>);
        {true, _, false} -> respond(Sock, 403, #{error => bad_origin});
        {true, _, true} ->
            ok = gen_tcp:send(Sock, [<<"HTTP/1.1 101 Switching Protocols\r\n"
                                       "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                                       "Sec-WebSocket-Accept: ">>,
                                     wschat_ws:accept_key(Key), <<"\r\n\r\n">>]),
            ws_join(Sock, string:trim(Nick))
    end.

%% Browsers don't apply the same-origin policy to WebSockets: any page can
%% open one to us *with the user's cookies*. Checking Origin against Host is
%% the standard defence (CSWSH). Non-browser clients send no Origin.
same_origin(H) ->
    case maps:find(<<"origin">>, H) of
        error -> true;
        {ok, Origin} ->
            case {uri_string:parse(Origin), maps:get(<<"host">>, H, <<>>)} of
                {#{host := OHost} = O, Host} ->
                    OPort = case O of #{port := P} -> [$: | integer_to_list(P)]; _ -> "" end,
                    string:lowercase(iolist_to_binary([OHost, OPort])) =:= string:lowercase(Host);
                _ -> false
            end
    end.

has_token(Value, Token) ->
    lists:member(Token, [string:lowercase(string:trim(T)) || T <- binary:split(Value, <<",">>, [global])]).

valid_key(Key) ->
    case catch base64:decode(Key) of
        B when is_binary(B), byte_size(B) =:= 16 -> true;
        _ -> false
    end.

ws_join(Sock, Nick) ->
    case wschat_room:join(self(), Nick) of
        {ok, History} ->
            send_event(Sock, #{type => welcome, nick => Nick,
                               history => History,
                               members => wschat_room:members()}),
            ok = inet:setopts(Sock, [{active, once}]),
            {ok, PI} = application:get_env(wschat, ping_interval),
            erlang:send_after(PI, self(), ping_tick),
            ws_loop(#{sock => Sock, buf => <<>>, frag => undefined,
                      alive => true, ping_interval => PI});
        {error, Reason} ->
            send_event(Sock, #{type => error, reason => Reason}),
            ws_close(Sock, 4000, atom_to_binary(Reason))
    end.

ws_loop(S = #{sock := Sock}) ->
    receive
        {tcp, Sock, Data} ->
            ok = inet:setopts(Sock, [{active, once}]),  % flow control: one packet at a time
            frames(S#{buf := <<(maps:get(buf, S))/binary, Data/binary>>, alive := true});
        {tcp_closed, Sock} -> ok;
        {tcp_error, Sock, _} -> ok;
        {chat_event, Ev} ->
            send_event(Sock, Ev),
            ws_loop(S);
        %% Heartbeat: if nothing (not even a pong) arrived since the last
        %% tick, the peer is gone -- a half-open TCP connection would
        %% otherwise keep a ghost member in the room forever.
        ping_tick when not map_get(alive, S) -> ws_close(Sock, 1001, <<"ping timeout">>);
        ping_tick ->
            gen_tcp:send(Sock, wschat_ws:encode(ping, <<>>)),
            erlang:send_after(maps:get(ping_interval, S), self(), ping_tick),
            ws_loop(S#{alive := false})
    end.

frames(S = #{sock := Sock, buf := Buf}) ->
    case wschat_ws:decode(Buf, ?MAX_WS_MESSAGE) of
        more -> ws_loop(S);
        {error, Code} -> ws_close(Sock, Code, <<>>);
        {ok, Frame, Rest} -> frame(Frame, S#{buf := Rest})
    end.

frame({_, ping, Payload}, S = #{sock := Sock}) ->
    gen_tcp:send(Sock, wschat_ws:encode(pong, Payload)),
    frames(S);
frame({_, pong, _}, S) -> frames(S);
frame({_, close, Payload}, #{sock := Sock}) ->
    %% Echo the peer's status code, then we're done.
    Code = case Payload of <<C:16, _/binary>> -> C; _ -> 1000 end,
    ws_close(Sock, Code, <<>>);
frame({Fin, Op, Payload}, S = #{frag := undefined}) when Op =:= text; Op =:= binary ->
    case Fin of
        1 -> message(Op, Payload, S);
        0 -> frames(S#{frag := {Op, Payload}})
    end;
frame({Fin, continuation, More}, S = #{frag := {Op, Acc}}) ->
    Msg = <<Acc/binary, More/binary>>,
    if
        byte_size(Msg) > ?MAX_WS_MESSAGE -> ws_close(maps:get(sock, S), 1009, <<>>);
        Fin =:= 1 -> message(Op, Msg, S#{frag := undefined});
        true -> frames(S#{frag := {Op, Msg}})
    end;
%% A new data frame mid-fragment, a stray continuation, or a reserved opcode.
frame(_, #{sock := Sock}) -> ws_close(Sock, 1002, <<>>).

message(binary, _, #{sock := Sock}) -> ws_close(Sock, 1003, <<"text only">>);
message(text, Text, S = #{sock := Sock}) ->
    case unicode:characters_to_binary(Text) of
        Text ->
            case wschat_room:input(self(), Text) of
                ok -> ok;
                {error, Reason} -> send_event(Sock, #{type => error, reason => Reason})
            end,
            frames(S);
        _ -> ws_close(Sock, 1007, <<"invalid utf-8">>)  % text frames must be UTF-8
    end.

ws_close(Sock, Code, Reason) ->
    gen_tcp:send(Sock, wschat_ws:encode_close(Code, Reason)),
    ok.  % serve/1 closes the socket; leaving this process ends room membership

%% ---- output -------------------------------------------------------------

%% Room events are already JSON-shaped maps, so they go out as they are.
send_event(Sock, Map) ->
    gen_tcp:send(Sock, wschat_ws:encode(text, wschat_json:encode(Map))).

respond(Sock, Status, none) -> send_response(Sock, Status, [], <<>>);
respond(Sock, Status, Map) ->
    send_response(Sock, Status, [{<<"content-type">>, <<"application/json">>}],
                  wschat_json:encode(Map)).

send_response(Sock, Status, Headers, Body) ->
    %% A 204 must not carry Content-Length (RFC 9110 section 8.6).
    Len = case Status of 204 -> []; _ -> [{<<"content-length">>, integer_to_binary(byte_size(Body))}] end,
    Hs = [[K, <<": ">>, V, <<"\r\n">>] || {K, V} <- Headers ++ Len ++
              [{<<"connection">>, <<"close">>}, {<<"cache-control">>, <<"no-store">>}]],
    gen_tcp:send(Sock, [<<"HTTP/1.1 ">>, integer_to_binary(Status), $\s, reason(Status), <<"\r\n">>,
                        Hs, <<"\r\n">>, Body]).

reason(101) -> <<"Switching Protocols">>;
reason(200) -> <<"OK">>;
reason(204) -> <<"No Content">>;
reason(400) -> <<"Bad Request">>;
reason(403) -> <<"Forbidden">>;
reason(404) -> <<"Not Found">>;
reason(405) -> <<"Method Not Allowed">>;
reason(409) -> <<"Conflict">>;
reason(413) -> <<"Payload Too Large">>;
reason(414) -> <<"URI Too Long">>;
reason(426) -> <<"Upgrade Required">>;
reason(431) -> <<"Request Header Fields Too Large">>;
reason(500) -> <<"Internal Server Error">>;
reason(501) -> <<"Not Implemented">>.
