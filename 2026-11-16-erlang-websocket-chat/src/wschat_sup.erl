%% Top-level supervisor. Order matters: the room must exist before any
%% session or connection tries to join it, and the listener goes last so we
%% never accept a socket we can't serve.
-module(wschat_sup).
-behaviour(supervisor).
-export([start_link/0, init/1]).

start_link() -> supervisor:start_link({local, ?MODULE}, ?MODULE, []).

init([]) ->
    {ok, Port} = application:get_env(wschat, port),
    Children =
        [#{id => wschat_room, start => {wschat_room, start_link, []}},
         #{id => wschat_poll_sup, start => {wschat_poll_sup, start_link, []},
           type => supervisor},
         #{id => wschat_listener, start => {wschat_listener, start_link, [Port]}}],
    {ok, {#{strategy => rest_for_one, intensity => 5, period => 10}, Children}}.
