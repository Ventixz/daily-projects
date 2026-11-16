%% The chat room: one gen_server that knows every member and fans events out.
%%
%% A member is just a pid. The room doesn't know or care whether that pid is
%% a WebSocket connection or a long-poll session -- both receive the same
%% {chat_event, Event} messages. That transport-independence is the whole
%% idea behind Bullet (WebSocket with a long-poll fallback behind one API).
-module(wschat_room).
-behaviour(gen_server).

-export([start_link/0, check/1, join/2, leave/1, say/2, rename/2, input/2, members/0, history/0]).
-export([valid_nick/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

-define(MAX_TEXT, 2000).

-record(st, {members = #{} :: #{pid() => {binary(), reference()}},
             history = queue:new(), hlen = 0, hmax, next_id = 1}).

start_link() -> gen_server:start_link({local, ?MODULE}, ?MODULE, [], []).

%% join(Pid, Nick) -> {ok, History} | {error, Reason}. Call it from the
%% member process itself: self() is what gets monitored and messaged.
join(Pid, Nick) -> gen_server:call(?MODULE, {join, Pid, Nick}).
%% Would this nick be accepted right now? (Advisory: join re-checks.)
check(Nick) -> gen_server:call(?MODULE, {check, Nick}).
leave(Pid) -> gen_server:call(?MODULE, {leave, Pid}).
say(Pid, Text) -> gen_server:call(?MODULE, {say, Pid, Text}).
rename(Pid, Nick) -> gen_server:call(?MODULE, {rename, Pid, Nick}).
members() -> gen_server:call(?MODULE, members).
history() -> gen_server:call(?MODULE, history).

%% The tiny text protocol both transports share: "/nick New" renames,
%% anything else is a message.
input(Pid, <<"/nick ", Nick/binary>>) -> rename(Pid, string:trim(Nick));
input(Pid, Text) -> say(Pid, Text).

%% 1-20 chars of letters, digits, '_' or '-'. Deliberately ASCII so two
%% nicks can't look identical with different code points.
valid_nick(N) when is_binary(N), byte_size(N) >= 1, byte_size(N) =< 20 ->
    lists:all(fun(C) -> (C >= $a andalso C =< $z) orelse (C >= $A andalso C =< $Z)
                            orelse (C >= $0 andalso C =< $9) orelse C =:= $_ orelse C =:= $-
              end, binary_to_list(N));
valid_nick(_) -> false.

init([]) ->
    {ok, Max} = application:get_env(wschat, history_size),
    {ok, #st{hmax = Max}}.

handle_call({join, Pid, Nick}, _From, S = #st{members = M}) ->
    case check_nick(Nick, M) of
        ok when is_map_key(Pid, M) -> {reply, {error, already_joined}, S};
        ok ->
            Ref = erlang:monitor(process, Pid),
            Hist = queue:to_list(S#st.history),  % history *before* our join
            S1 = S#st{members = M#{Pid => {Nick, Ref}}},
            {reply, {ok, Hist}, publish(#{type => join, nick => Nick}, S1)};
        Err -> {reply, Err, S}
    end;
handle_call({check, Nick}, _From, S) ->
    {reply, check_nick(Nick, S#st.members), S};
handle_call({leave, Pid}, _From, S) ->
    {reply, ok, drop(Pid, S)};
handle_call({say, Pid, Text}, _From, S = #st{members = M}) ->
    T = string:trim(Text),
    case M of
        #{Pid := {Nick, _}} when T =/= <<>>, byte_size(T) =< ?MAX_TEXT ->
            {reply, ok, publish(#{type => msg, nick => Nick, text => T}, S)};
        #{Pid := _} when T =:= <<>> -> {reply, {error, empty}, S};
        #{Pid := _} -> {reply, {error, too_long}, S};
        _ -> {reply, {error, not_joined}, S}
    end;
handle_call({rename, Pid, New}, _From, S = #st{members = M}) ->
    case M of
        #{Pid := {New, _}} -> {reply, ok, S};
        #{Pid := {Old, Ref}} ->
            case check_nick(New, M) of
                ok ->
                    S1 = S#st{members = M#{Pid => {New, Ref}}},
                    {reply, ok, publish(#{type => nick, nick => New, old => Old}, S1)};
                Err -> {reply, Err, S}
            end;
        _ -> {reply, {error, not_joined}, S}
    end;
handle_call(members, _From, S) ->
    {reply, lists:sort([N || {N, _} <- maps:values(S#st.members)]), S};
handle_call(history, _From, S) ->
    {reply, queue:to_list(S#st.history), S}.

handle_cast(_, S) -> {noreply, S}.

%% A member died (tab closed, socket dropped, session expired): announce it.
%% Monitors mean no member has to remember to say goodbye.
handle_info({'DOWN', _Ref, process, Pid, _}, S) -> {noreply, drop(Pid, S)};
handle_info(_, S) -> {noreply, S}.

check_nick(Nick, Members) ->
    case valid_nick(Nick) of
        false -> {error, invalid_nick};
        true ->
            Lower = string:lowercase(Nick),
            Taken = [x || {N, _} <- maps:values(Members), string:lowercase(N) =:= Lower],
            case Taken of [] -> ok; _ -> {error, nick_taken} end
    end.

drop(Pid, S = #st{members = M}) ->
    case maps:take(Pid, M) of
        {{Nick, Ref}, M1} ->
            erlang:demonitor(Ref, [flush]),
            publish(#{type => leave, nick => Nick}, S#st{members = M1});
        error -> S
    end.

%% Stamp the event with an id and time, keep it in the bounded history,
%% and send it to every member (including the sender, so clients render
%% their own messages only once the server has accepted them).
publish(Ev0, S = #st{next_id = Id, history = H, hlen = L, hmax = Max}) ->
    Ev = Ev0#{id => Id, ts => erlang:system_time(millisecond)},
    [P ! {chat_event, Ev} || P <- maps:keys(S#st.members)],
    {H1, L1} = case L >= Max of
                   true -> {queue:in(Ev, queue:drop(H)), L};
                   false -> {queue:in(Ev, H), L + 1}
               end,
    S#st{next_id = Id + 1, history = H1, hlen = L1}.
