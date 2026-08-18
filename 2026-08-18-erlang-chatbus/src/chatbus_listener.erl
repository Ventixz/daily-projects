%% AI-written implementation, built for the daily-projects practice repo.
-module(chatbus_listener).

-export([start_link/1]).
-export([listen/2]).

%% Not a gen_server: this process spends its life blocked inside
%% gen_tcp:accept/1, which has nothing to do with handling a gen_server
%% message queue. proc_lib + init_ack gives it a supervisor-compatible
%% start_link/1 without pretending it's a gen_server underneath.
start_link(Port) ->
    proc_lib:start_link(?MODULE, listen, [self(), Port]).

listen(Parent, Port) ->
    {ok, ListenSocket} = gen_tcp:listen(Port, [binary, {packet, line},
                                                {active, false},
                                                {reuseaddr, true}]),
    proc_lib:init_ack(Parent, {ok, self()}),
    accept_loop(ListenSocket).

accept_loop(ListenSocket) ->
    case gen_tcp:accept(ListenSocket) of
        {ok, Socket} ->
            {ok, ClientPid} = chatbus_client_sup:start_client(Socket),
            ok = gen_tcp:controlling_process(Socket, ClientPid),
            gen_server:cast(ClientPid, activate),
            accept_loop(ListenSocket);
        {error, closed} ->
            ok;
        {error, Reason} ->
            error_logger:error_msg("chatbus_listener accept error: ~p~n", [Reason]),
            accept_loop(ListenSocket)
    end.
