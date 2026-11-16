%% End-to-end tests over real TCP sockets: the full app is started on a
%% random port and driven by a hand-rolled HTTP client and WebSocket client.
-module(wschat_http_tests).
-include_lib("eunit/include/eunit.hrl").

http_test_() ->
    {foreach, fun setup/0, fun cleanup/1,
     [fun static_files/0,
      fun not_found_and_wrong_method/0,
      fun oversized_body_rejected/0,
      fun handshake_validation/0,
      fun two_websocket_clients_chat/0,
      fun websocket_nick_taken_closes_with_reason/0,
      fun websocket_control_frames_and_fragments/0,
      fun websocket_protocol_violations_close/0,
      fun websocket_disconnect_announces_leave/0,
      fun heartbeat_drops_silent_peer/0,
      fun long_poll_and_websocket_share_a_room/0,
      fun long_poll_times_out_empty/0,
      fun long_poll_errors/0,
      fun idle_poll_session_expires/0]}.

setup() ->
    application:load(wschat),
    [application:set_env(wschat, K, V) ||
        {K, V} <- [{port, 0}, {poll_timeout, 300}, {session_idle, 400},
                   {ping_interval, 30000}, {history_size, 50}]],
    {ok, _} = application:ensure_all_started(wschat),
    ok.

cleanup(_) -> application:stop(wschat).

port() -> wschat_listener:port().

%% ---- tests --------------------------------------------------------------

static_files() ->
    {200, H, Body} = http(get, "/", [], <<>>),
    ?assertEqual(<<"text/html; charset=utf-8">>, maps:get(<<"content-type">>, H)),
    ?assertMatch({_, _}, binary:match(maps:get(<<"content-security-policy">>, H), <<"default-src 'self'">>)),
    ?assertMatch({_, _}, binary:match(Body, <<"<script src=\"/chat.js\">">>)),
    ?assertMatch({200, _, <<"// Browser side", _/binary>>}, http(get, "/chat.js", [], <<>>)),
    {200, CH, _} = http(get, "/chat.css", [], <<>>),
    ?assertEqual(<<"text/css; charset=utf-8">>, maps:get(<<"content-type">>, CH)),
    %% The CSP forbids inline <style>/<script>, so the page must not use them.
    ?assertEqual(nomatch, binary:match(Body, [<<"<style">>, <<"<script>">>])).

not_found_and_wrong_method() ->
    ?assertMatch({404, _, <<"{\"error\":\"not_found\"}">>}, http(get, "/nope", [], <<>>)),
    ?assertMatch({405, _, _}, http(delete, "/", [], <<>>)),
    ?assertMatch({405, _, _}, http(get, "/poll/join", [], <<>>)).

oversized_body_rejected() ->
    ?assertMatch({413, _, _}, http(post, "/poll/join", [], binary:copy(<<"a">>, 20000))).

handshake_validation() ->
    Good = [{"Upgrade", "websocket"}, {"Connection", "keep-alive, Upgrade"},
            {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="}, {"Sec-WebSocket-Version", "13"}],
    ?assertMatch({400, _, _}, http(get, "/ws?nick=a", lists:keydelete("Sec-WebSocket-Key", 1, Good), <<>>)),
    ?assertMatch({400, _, _}, http(get, "/ws?nick=a", lists:keyreplace("Sec-WebSocket-Key", 1, Good,
                                                                         {"Sec-WebSocket-Key", "c2hvcnQ="}), <<>>)),
    {426, H, _} = http(get, "/ws?nick=a", lists:keyreplace("Sec-WebSocket-Version", 1, Good,
                                                           {"Sec-WebSocket-Version", "8"}), <<>>),
    ?assertEqual(<<"13">>, maps:get(<<"sec-websocket-version">>, H)),
    %% Cross-site WebSocket hijacking: a page on another origin is refused.
    ?assertMatch({403, _, <<"{\"error\":\"bad_origin\"}">>},
                 http(get, "/ws?nick=a", [{"Origin", "http://evil.example"} | Good], <<>>)),
    %% Same origin (Origin host:port == Host) is fine.
    Origin = "http://localhost:" ++ integer_to_list(port()),
    S = ws_connect("ada", [{"Origin", Origin}]),
    ?assertMatch({text, <<"{\"history\":", _/binary>>}, ws_recv(S)).

two_websocket_clients_chat() ->
    A = ws_connect("ada"),
    {text, W} = ws_recv(A),
    assert_has(W, [<<"\"type\":\"welcome\"">>, <<"\"members\":[\"ada\"]">>]),
    assert_has(element(2, ws_recv(A)), [<<"\"type\":\"join\"">>, <<"\"nick\":\"ada\"">>]),
    B = ws_connect("bob"),
    {text, WB} = ws_recv(B),
    %% Bob's welcome replays Ada's join from history.
    assert_has(WB, [<<"\"members\":[\"ada\",\"bob\"]">>, <<"\"nick\":\"ada\"">>]),
    assert_has(element(2, ws_recv(A)), [<<"\"type\":\"join\"">>, <<"\"nick\":\"bob\"">>]),
    _ = ws_recv(B),                                   % bob's own join
    ws_send(B, text, <<"hello \"ada\" </script>">>),
    [assert_has(element(2, ws_recv(X)), [<<"\"type\":\"msg\"">>, <<"\"nick\":\"bob\"">>,
                                         <<"\"text\":\"hello \\\"ada\\\" \\u003c/script>\"">>])
     || X <- [A, B]],
    ws_send(A, text, <<"/nick ada_l">>),
    [assert_has(element(2, ws_recv(X)), [<<"\"old\":\"ada\"">>, <<"\"nick\":\"ada_l\"">>]) || X <- [A, B]],
    ws_send(A, text, <<"   ">>),
    ?assertEqual({text, <<"{\"reason\":\"empty\",\"type\":\"error\"}">>}, ws_recv(A)).

websocket_nick_taken_closes_with_reason() ->
    _ = ws_connect("ada"),
    S = ws_connect("ADA"),
    ?assertEqual({text, <<"{\"reason\":\"nick_taken\",\"type\":\"error\"}">>}, ws_recv(S)),
    ?assertEqual({close, <<4000:16, "nick_taken">>}, ws_recv(S)),
    ?assertEqual(closed, ws_recv(S)).

websocket_control_frames_and_fragments() ->
    S = ws_connect("ada"),
    _ = ws_recv(S), _ = ws_recv(S),
    ws_send(S, ping, <<"are you there">>),
    ?assertEqual({pong, <<"are you there">>}, ws_recv(S)),
    %% "frag" + "mented", with a ping interleaved between fragments (allowed).
    gen_tcp:send(S, [wschat_ws_tests:mask(0, 1, <<"frag">>), wschat_ws_tests:mask(1, 9, <<"p">>),
                     wschat_ws_tests:mask(1, 0, <<"mented">>)]),
    ?assertEqual({pong, <<"p">>}, ws_recv(S)),
    assert_has(element(2, ws_recv(S)), [<<"\"text\":\"fragmented\"">>]),
    %% A frame split across TCP writes is reassembled.
    F = wschat_ws_tests:mask(1, 1, <<"split">>),
    gen_tcp:send(S, binary:part(F, 0, 3)), timer:sleep(20),
    gen_tcp:send(S, binary:part(F, 3, byte_size(F) - 3)),
    assert_has(element(2, ws_recv(S)), [<<"\"text\":\"split\"">>]),
    ws_send(S, close, <<1000:16>>),
    ?assertEqual({close, <<1000:16>>}, ws_recv(S)),
    ?assertEqual(closed, ws_recv(S)).

websocket_protocol_violations_close() ->
    Cases = [{<<16#81, 2, "hi">>, 1002},                                  % unmasked
             {wschat_ws_tests:mask(1, 1, <<"bad ", 16#ff, 16#fe>>), 1007},  % invalid UTF-8
             {wschat_ws_tests:mask(1, 2, <<1, 2, 3>>), 1003},                % binary
             {wschat_ws_tests:mask(1, 0, <<"x">>), 1002},                    % stray continuation
             {wschat_ws_tests:mask(1, 3, <<>>), 1002}],                      % reserved opcode
    lists:foreach(
      fun({{Frame, Code}, N}) ->
              S = ws_connect("u" ++ integer_to_list(N)),
              _ = ws_recv(S), _ = ws_recv(S),
              gen_tcp:send(S, Frame),
              ?assertMatch({close, <<Code:16, _/binary>>}, ws_recv(S)),
              ?assertEqual(closed, ws_recv(S))
      end, lists:zip(Cases, lists:seq(1, length(Cases)))).

websocket_disconnect_announces_leave() ->
    A = ws_connect("ada"),
    _ = ws_recv(A), _ = ws_recv(A),
    B = ws_connect("bob"),
    _ = ws_recv(A),
    gen_tcp:close(B),   % no close frame: just a dropped connection
    assert_has(element(2, ws_recv(A)), [<<"\"type\":\"leave\"">>, <<"\"nick\":\"bob\"">>]),
    wait_until(fun() -> wschat_room:members() =:= [<<"ada">>] end).

heartbeat_drops_silent_peer() ->
    application:set_env(wschat, ping_interval, 100),
    S = ws_connect("ghost"),
    _ = ws_recv(S), _ = ws_recv(S),
    ?assertEqual({ping, <<>>}, ws_recv(S)),           % we never answer...
    ?assertEqual({close, <<1001:16, "ping timeout">>}, ws_recv(S)),
    ?assertEqual(closed, ws_recv(S)),
    wait_until(fun() -> wschat_room:members() =:= [] end).

long_poll_and_websocket_share_a_room() ->
    {200, _, J} = http(post, "/poll/join", [], <<" pat \n">>),
    assert_has(J, [<<"\"nick\":\"pat\"">>, <<"\"members\":[\"pat\"]">>]),
    Sid = sid(J),
    Hdr = [{"X-Chat-Session", Sid}],
    {200, _, E1} = http(get, "/poll/recv", Hdr, <<>>),          % pat's own join is waiting
    assert_has(E1, [<<"\"type\":\"join\"">>, <<"\"nick\":\"pat\"">>]),
    A = ws_connect("ada"),
    _ = ws_recv(A), _ = ws_recv(A),
    {200, _, E1b} = http(get, "/poll/recv", Hdr, <<>>),         % ada's join, buffered
    assert_has(E1b, [<<"\"type\":\"join\"">>, <<"\"nick\":\"ada\"">>]),
    %% A parked poll is answered as soon as an event arrives, not at timeout.
    Me = self(),
    spawn(fun() -> Me ! {poll, http(get, "/poll/recv", Hdr, <<>>)} end),
    timer:sleep(50),
    ws_send(A, text, <<"hi pat">>),
    receive {poll, {200, _, E2}} -> assert_has(E2, [<<"\"text\":\"hi pat\"">>, <<"\"nick\":\"ada\"">>])
    after 250 -> error(poll_not_released_early)
    end,
    _ = ws_recv(A),                                              % ada sees her own msg
    ?assertMatch({204, _, <<>>}, http(post, "/poll/send", Hdr, <<"hi ada">>)),
    assert_has(element(2, ws_recv(A)), [<<"\"text\":\"hi ada\"">>, <<"\"nick\":\"pat\"">>]),
    ?assertMatch({204, _, <<>>}, http(post, "/poll/leave", Hdr, <<>>)),
    assert_has(element(2, ws_recv(A)), [<<"\"type\":\"leave\"">>, <<"\"nick\":\"pat\"">>]),
    ?assertMatch({404, _, _}, http(get, "/poll/recv", Hdr, <<>>)).

long_poll_times_out_empty() ->
    {200, _, J} = http(post, "/poll/join", [], <<"pat">>),
    Hdr = [{"X-Chat-Session", sid(J)}],
    {200, _, _} = http(get, "/poll/recv", Hdr, <<>>),            % drain own join
    T0 = erlang:monotonic_time(millisecond),
    ?assertEqual({200, <<"{\"events\":[]}">>}, element_1_3(http(get, "/poll/recv", Hdr, <<>>))),
    ?assert(erlang:monotonic_time(millisecond) - T0 >= 280).

long_poll_errors() ->
    ?assertMatch({404, _, <<"{\"error\":\"no_session\"}">>}, http(get, "/poll/recv", [], <<>>)),
    ?assertMatch({404, _, _}, http(get, "/poll/recv", [{"X-Chat-Session", "forged"}], <<>>)),
    ?assertMatch({400, _, <<"{\"error\":\"invalid_nick\"}">>}, http(post, "/poll/join", [], <<"two words">>)),
    {200, _, J} = http(post, "/poll/join", [], <<"pat">>),
    ?assertMatch({409, _, <<"{\"error\":\"nick_taken\"}">>}, http(post, "/poll/join", [], <<"Pat">>)),
    ?assertMatch({400, _, <<"{\"error\":\"empty\"}">>},
                 http(post, "/poll/send", [{"X-Chat-Session", sid(J)}], <<" ">>)).

idle_poll_session_expires() ->
    A = ws_connect("ada"),
    _ = ws_recv(A), _ = ws_recv(A),
    {200, _, J} = http(post, "/poll/join", [], <<"pat">>),
    _ = ws_recv(A),                                              % pat joined
    %% pat never polls; after session_idle (400ms) the session ends.
    assert_has(element(2, ws_recv(A, 2000)), [<<"\"type\":\"leave\"">>, <<"\"nick\":\"pat\"">>]),
    ?assertMatch({404, _, _}, http(get, "/poll/recv", [{"X-Chat-Session", sid(J)}], <<>>)).

%% ---- tiny HTTP client ---------------------------------------------------

http(Method, Path, Headers, Body) ->
    {ok, S} = gen_tcp:connect("127.0.0.1", port(), [binary, {active, false}]),
    M = string:uppercase(atom_to_list(Method)),
    Hs = [[K, ": ", V, "\r\n"] || {K, V} <- Headers],
    ok = gen_tcp:send(S, [M, " ", Path, " HTTP/1.1\r\nHost: localhost\r\n", Hs,
                          "Content-Length: ", integer_to_list(byte_size(Body)), "\r\n\r\n", Body]),
    Resp = recv_all(S, <<>>),
    gen_tcp:close(S),
    [Head, RespBody] = binary:split(Resp, <<"\r\n\r\n">>),
    [<<"HTTP/1.1 ", Code:3/binary, _/binary>> | Lines] = binary:split(Head, <<"\r\n">>, [global]),
    RH = maps:from_list([begin [K, V] = binary:split(L, <<": ">>), {string:lowercase(K), V} end
                         || L <- Lines]),
    {binary_to_integer(Code), RH, RespBody}.

recv_all(S, Acc) ->
    case gen_tcp:recv(S, 0, 5000) of
        {ok, D} -> recv_all(S, <<Acc/binary, D/binary>>);
        {error, closed} -> Acc
    end.

element_1_3({A, _, C}) -> {A, C}.

sid(Json) ->
    {match, [Sid]} = re:run(Json, <<"\"sid\":\"([^\"]+)\"">>, [{capture, all_but_first, list}]),
    Sid.

%% ---- tiny WebSocket client ----------------------------------------------

ws_connect(Nick) -> ws_connect(Nick, []).
ws_connect(Nick, Extra) ->
    {ok, S} = gen_tcp:connect("127.0.0.1", port(), [binary, {active, false}]),
    Key = base64:encode(crypto:strong_rand_bytes(16)),
    Hs = [[K, ": ", V, "\r\n"] || {K, V} <- Extra],
    ok = gen_tcp:send(S, ["GET /ws?nick=", Nick, " HTTP/1.1\r\nHost: localhost:", integer_to_list(port()),
                          "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ", Key,
                          "\r\nSec-WebSocket-Version: 13\r\n", Hs, "\r\n"]),
    {Head, Rest} = read_head(S, <<>>),
    ?assertMatch(<<"HTTP/1.1 101 ", _/binary>>, Head),
    Accept = wschat_ws:accept_key(Key),
    ?assertMatch({_, _}, binary:match(Head, <<"Sec-WebSocket-Accept: ", Accept/binary>>)),
    put({buf, S}, Rest),
    S.

read_head(S, Acc) ->
    case binary:split(Acc, <<"\r\n\r\n">>) of
        [Head, Rest] -> {Head, Rest};
        [_] -> {ok, D} = gen_tcp:recv(S, 0, 2000), read_head(S, <<Acc/binary, D/binary>>)
    end.

ws_send(S, Type, Payload) ->
    Op = case Type of text -> 1; close -> 8; ping -> 9 end,
    ok = gen_tcp:send(S, wschat_ws_tests:mask(1, Op, Payload)).

ws_recv(S) -> ws_recv(S, 1000).
ws_recv(S, Timeout) ->
    Buf = get({buf, S}),
    case server_frame(Buf) of
        {Frame, Rest} -> put({buf, S}, Rest), Frame;
        more ->
            case gen_tcp:recv(S, 0, Timeout) of
                {ok, D} -> put({buf, S}, <<Buf/binary, D/binary>>), ws_recv(S, Timeout);
                {error, closed} when Buf =:= <<>> -> closed;
                {error, E} -> error({ws_recv, E, Buf})
            end
    end.

%% Server frames: never masked, never fragmented by this server.
server_frame(<<1:1, 0:3, Op:4, 0:1, 126:7, Len:16, Rest/binary>>) -> take(Op, Len, Rest);
server_frame(<<1:1, 0:3, Op:4, 0:1, 127:7, Len:64, Rest/binary>>) -> take(Op, Len, Rest);
server_frame(<<1:1, 0:3, Op:4, 0:1, Len:7, Rest/binary>>) when Len < 126 -> take(Op, Len, Rest);
server_frame(_) -> more.

take(Op, Len, Bin) ->
    case Bin of
        <<P:Len/binary, Rest/binary>> ->
            T = case Op of 1 -> text; 8 -> close; 9 -> ping; 10 -> pong end,
            {{T, P}, Rest};
        _ -> more
    end.

assert_has(Bin, Needles) ->
    [case binary:match(Bin, N) of
         nomatch -> error({missing, N, in, Bin});
         _ -> ok
     end || N <- Needles],
    ok.

wait_until(F) -> wait_until(F, 50).
wait_until(F, 0) -> ?assert(F());
wait_until(F, N) -> case F() of true -> ok; false -> timer:sleep(20), wait_until(F, N - 1) end.
