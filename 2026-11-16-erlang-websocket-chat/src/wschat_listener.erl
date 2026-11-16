%% Owns the listening socket and runs one acceptor process.
%% Each accepted socket gets its own process (wschat_conn) -- the classic
%% Erlang "process per connection" model, where a crash in one connection
%% can't touch any other.
-module(wschat_listener).
-behaviour(gen_server).
-export([start_link/1, port/0]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

start_link(Port) -> gen_server:start_link({local, ?MODULE}, ?MODULE, Port, []).

%% The actual port (useful when configured with 0 = "pick a free one").
port() -> gen_server:call(?MODULE, port).

init(Port) ->
    {ok, LSock} = gen_tcp:listen(Port, [binary, {active, false}, {reuseaddr, true},
                                        {backlog, 128}, {nodelay, true}]),
    {ok, Actual} = inet:port(LSock),
    Acceptor = spawn_link(fun() -> accept_loop(LSock) end),
    {ok, #{lsock => LSock, port => Actual, acceptor => Acceptor}}.

handle_call(port, _From, S = #{port := P}) -> {reply, P, S}.
handle_cast(_, S) -> {noreply, S}.
handle_info(_, S) -> {noreply, S}.

accept_loop(LSock) ->
    case gen_tcp:accept(LSock) of
        {ok, Sock} ->
            %% The new process must own the socket before it reads from it,
            %% or {tcp, ...} messages would land in *our* mailbox.
            Pid = spawn(fun() -> receive {go, S} -> wschat_conn:serve(S) end end),
            ok = gen_tcp:controlling_process(Sock, Pid),
            Pid ! {go, Sock},
            accept_loop(LSock);
        {error, closed} ->
            ok;
        {error, _} ->
            accept_loop(LSock)
    end.
