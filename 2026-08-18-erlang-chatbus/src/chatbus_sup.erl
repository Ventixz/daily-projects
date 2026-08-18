%% AI-written implementation, built for the daily-projects practice repo.
-module(chatbus_sup).
-behaviour(supervisor).

-export([start_link/0]).
-export([init/1]).

start_link() ->
    supervisor:start_link({local, ?MODULE}, ?MODULE, []).

init([]) ->
    Port = application:get_env(chatbus, port, 5555),
    SupFlags = #{strategy => one_for_one, intensity => 10, period => 10},
    %% Order matters under simple one_for_one restart: chatbus_listener
    %% hands every accepted socket to chatbus_client_sup, so client_sup
    %% must already be alive by the time the listener can possibly reach
    %% it. supervisor:init/1 starts children in list order and blocks on
    %% each one's init before starting the next, so this list order is
    %% the whole guarantee -- no separate synchronization needed.
    Children = [
        #{id => chatbus_room_sup,
          start => {chatbus_room_sup, start_link, []},
          restart => permanent, shutdown => 5000,
          type => supervisor, modules => [chatbus_room_sup]},
        #{id => chatbus_client_sup,
          start => {chatbus_client_sup, start_link, []},
          restart => permanent, shutdown => 5000,
          type => supervisor, modules => [chatbus_client_sup]},
        #{id => chatbus_listener,
          start => {chatbus_listener, start_link, [Port]},
          restart => permanent, shutdown => 5000,
          type => worker, modules => [chatbus_listener]}
    ],
    {ok, {SupFlags, Children}}.
