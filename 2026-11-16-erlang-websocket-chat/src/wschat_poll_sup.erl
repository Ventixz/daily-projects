%% Supervises long-poll sessions and owns the sid -> pid lookup table.
%% The supervisor process lives as long as the sessions do, which makes it
%% a reasonable owner for the ETS table (it dies with them, too).
-module(wschat_poll_sup).
-behaviour(supervisor).
-export([start_link/0, start_session/1, lookup/1, init/1]).

-define(TAB, wschat_sessions).

start_link() -> supervisor:start_link({local, ?MODULE}, ?MODULE, []).

start_session(Nick) -> supervisor:start_child(?MODULE, [Nick]).

lookup(Sid) when is_binary(Sid) ->
    case ets:lookup(?TAB, Sid) of
        [{_, Pid}] -> {ok, Pid};
        [] -> error
    end;
lookup(_) -> error.

init([]) ->
    ets:new(?TAB, [named_table, public, set, {read_concurrency, true}]),
    {ok, {#{strategy => simple_one_for_one, intensity => 10, period => 10},
          [#{id => session, start => {wschat_poll_session, start_link, []},
             restart => temporary}]}}.
