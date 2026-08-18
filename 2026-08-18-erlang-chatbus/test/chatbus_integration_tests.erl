%% Exercises the whole OTP tree (application -> chatbus_sup ->
%% listener/room_sup/client_sup) over real TCP sockets, not just the
%% gen_server logic in isolation.
-module(chatbus_integration_tests).
-include_lib("eunit/include/eunit.hrl").

-define(PORT, 5561).

chat_roundtrip_test_() ->
    {setup, fun start_app/0, fun stop_app/1, fun(_) -> [?_test(roundtrip())] end}.

start_app() ->
    ok = application:load(chatbus),
    ok = application:set_env(chatbus, port, ?PORT),
    {ok, _Started} = application:ensure_all_started(chatbus),
    ok.

stop_app(_) ->
    application:stop(chatbus),
    application:unload(chatbus).

roundtrip() ->
    Alice = connect(),
    ?assertMatch("* welcome" ++ _, recv_line(Alice)),
    send(Alice, "NICK alice"),
    ?assertEqual("* nick set to alice", recv_line(Alice)),
    send(Alice, "JOIN lobby"),
    ?assertEqual("* joined lobby", recv_line(Alice)),

    Bob = connect(),
    _Welcome = recv_line(Bob),
    send(Bob, "NICK bob"),
    _NickAck = recv_line(Bob),
    send(Bob, "JOIN lobby"),
    _JoinAck = recv_line(Bob),
    ?assertEqual("* bob joined", recv_line(Alice)),

    %% broadcast/3 reaches every member of the room, including whoever
    %% sent it -- each client sees their own line echoed back, same as
    %% every other message. Both sockets have to drain their own echo
    %% before the next assertion or the queues desync.
    send(Bob, "MSG hello room"),
    ?assertEqual("bob: hello room", recv_line(Alice)),
    ?assertEqual("bob: hello room", recv_line(Bob)),

    send(Alice, "MSG hi back"),
    ?assertEqual("alice: hi back", recv_line(Bob)),
    ?assertEqual("alice: hi back", recv_line(Alice)),

    send(Bob, "QUIT"),
    ?assertEqual("* bye", recv_line(Bob)),
    ?assertEqual("* bob left", recv_line(Alice)),

    gen_tcp:close(Alice),
    ok.

message_before_join_is_rejected_test_() ->
    {setup, fun start_app/0, fun stop_app/1,
     fun(_) -> [?_test(begin
         Sock = connect(),
         _Welcome = recv_line(Sock),
         send(Sock, "MSG too early"),
         ?assertEqual("! join a room first: JOIN <room>", recv_line(Sock)),
         gen_tcp:close(Sock)
     end)]
     end}.

connect() ->
    {ok, Sock} = gen_tcp:connect("localhost", ?PORT, [binary, {packet, line}, {active, false}]),
    Sock.

send(Sock, Line) ->
    ok = gen_tcp:send(Sock, [Line, "\n"]).

recv_line(Sock) ->
    {ok, Data} = gen_tcp:recv(Sock, 0, 2000),
    string:trim(binary_to_list(Data)).
