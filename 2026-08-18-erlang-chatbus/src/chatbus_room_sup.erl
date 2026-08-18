%% AI-written implementation, built for the daily-projects practice repo.
-module(chatbus_room_sup).
-behaviour(supervisor).

-export([start_link/0, get_or_start_room/1]).
-export([init/1]).

-define(TABLE, chatbus_room_registry).

start_link() ->
    supervisor:start_link({local, ?MODULE}, ?MODULE, []).

init([]) ->
    ets:new(?TABLE, [named_table, public, set, {read_concurrency, true}]),
    SupFlags = #{strategy => simple_one_for_one, intensity => 10, period => 10},
    ChildSpec = #{id => chatbus_room,
                  start => {chatbus_room, start_link, []},
                  restart => temporary,
                  shutdown => 5000,
                  type => worker,
                  modules => [chatbus_room]},
    {ok, {SupFlags, [ChildSpec]}}.

%% Rooms are created on first JOIN and never torn down again -- a
%% deliberate scope cut, see LEARNING.md. Two clients racing to create
%% the same room both call supervisor:start_child/2 successfully (a
%% supervisor doesn't dedupe by argument), so the dedup has to happen
%% here, on the ets table, and ets:insert_new/2 is what makes "first
%% writer wins" atomic across two processes doing this concurrently.
get_or_start_room(Name) ->
    case ets:lookup(?TABLE, Name) of
        [{Name, Pid}] ->
            case is_process_alive(Pid) of
                true -> {ok, Pid};
                false -> ets:delete(?TABLE, Name), start_and_register(Name)
            end;
        [] ->
            start_and_register(Name)
    end.

start_and_register(Name) ->
    case supervisor:start_child(?MODULE, [Name]) of
        {ok, Pid} ->
            case ets:insert_new(?TABLE, {Name, Pid}) of
                true ->
                    {ok, Pid};
                false ->
                    %% Lost the race: another process's room already won
                    %% the ets slot between our lookup and our insert.
                    %% Discard the room we just started and use theirs.
                    supervisor:terminate_child(?MODULE, Pid),
                    [{Name, Winner}] = ets:lookup(?TABLE, Name),
                    {ok, Winner}
            end;
        {error, _Reason} = Error ->
            Error
    end.
