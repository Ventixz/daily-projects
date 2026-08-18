%% AI-written implementation, built for the daily-projects practice repo.
-module(chatbus_client_sup).
-behaviour(supervisor).

-export([start_link/0, start_client/1]).
-export([init/1]).

start_link() ->
    supervisor:start_link({local, ?MODULE}, ?MODULE, []).

start_client(Socket) ->
    supervisor:start_child(?MODULE, [Socket]).

init([]) ->
    SupFlags = #{strategy => simple_one_for_one, intensity => 10, period => 10},
    ChildSpec = #{id => chatbus_client,
                  start => {chatbus_client, start_link, []},
                  restart => temporary,
                  shutdown => 5000,
                  type => worker,
                  modules => [chatbus_client]},
    {ok, {SupFlags, [ChildSpec]}}.
