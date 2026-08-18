%% AI-written implementation, built for the daily-projects practice repo.
-module(chatbus_room).
-behaviour(gen_server).

-export([start_link/1, join/3, leave/2, broadcast/3, members/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

-record(state, {name, members = #{}}). % Members :: #{pid() => {Nick :: string(), monitor()}}

%% API

start_link(Name) ->
    gen_server:start_link(?MODULE, Name, []).

join(RoomPid, Pid, Nick) ->
    gen_server:call(RoomPid, {join, Pid, Nick}).

leave(RoomPid, Pid) ->
    gen_server:call(RoomPid, {leave, Pid}).

broadcast(RoomPid, FromNick, Text) ->
    gen_server:cast(RoomPid, {broadcast, FromNick, Text}).

members(RoomPid) ->
    gen_server:call(RoomPid, members).

%% gen_server callbacks

init(Name) ->
    {ok, #state{name = Name}}.

handle_call({join, Pid, Nick}, _From, State = #state{members = Members}) ->
    case maps:is_key(Pid, Members) of
        true ->
            {reply, {error, already_joined}, State};
        false ->
            Ref = erlang:monitor(process, Pid),
            NewMembers = Members#{Pid => {Nick, Ref}},
            notify_others(Members, Pid, {room_event, self(), joined, Nick}),
            {reply, ok, State#state{members = NewMembers}}
    end;
handle_call({leave, Pid}, _From, State = #state{members = Members}) ->
    case maps:take(Pid, Members) of
        error ->
            {reply, {error, not_member}, State};
        {{Nick, Ref}, NewMembers} ->
            erlang:demonitor(Ref, [flush]),
            notify_others(NewMembers, Pid, {room_event, self(), left, Nick}),
            {reply, ok, State#state{members = NewMembers}}
    end;
handle_call(members, _From, State = #state{members = Members}) ->
    Nicks = [Nick || {Nick, _Ref} <- maps:values(Members)],
    {reply, Nicks, State}.

handle_cast({broadcast, FromNick, Text}, State = #state{members = Members}) ->
    Msg = {chat_msg, self(), FromNick, Text},
    [Pid ! Msg || Pid <- maps:keys(Members)],
    {noreply, State};
handle_cast(_Msg, State) ->
    {noreply, State}.

%% A member's process dying is not an error path here -- it's how a client
%% disconnecting is discovered at all when the socket owner never called
%% leave/2 (crashed, network drop, kill -9). The room is a passive observer
%% of client lifetime via monitor/2, not the thing enforcing it.
handle_info({'DOWN', Ref, process, Pid, _Reason}, State = #state{members = Members}) ->
    case maps:take(Pid, Members) of
        error ->
            {noreply, State};
        {{Nick, Ref}, NewMembers} ->
            notify_others(NewMembers, Pid, {room_event, self(), left, Nick}),
            {noreply, State#state{members = NewMembers}};
        {_OtherRefPair, _} ->
            {noreply, State}
    end;
handle_info(_Msg, State) ->
    {noreply, State}.

terminate(_Reason, _State) ->
    ok.

code_change(_OldVsn, State, _Extra) ->
    {ok, State}.

%% Internal

notify_others(Members, ExceptPid, Msg) ->
    [Pid ! Msg || Pid <- maps:keys(Members), Pid =/= ExceptPid],
    ok.
