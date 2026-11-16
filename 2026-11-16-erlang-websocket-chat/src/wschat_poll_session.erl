%% A long-poll client's stand-in inside the server.
%%
%% The browser can't hold a connection open, so this process holds its
%% place in the room instead: it is the room member, buffers events, and
%% hands them over whenever the browser polls. If the browser stops
%% polling for `session_idle` ms, the process exits and the room's monitor
%% announces the leave -- exactly as if a WebSocket had closed.
-module(wschat_poll_session).
-behaviour(gen_server).
-export([start_link/1, recv/1, input/2, stop/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2]).

-record(st, {sid, welcome, buf = [], waiter, wait_timer, idle_timer, poll_timeout, idle}).

%% Returns {ok, Pid, {Sid, History}} so supervisor:start_child/2 hands the
%% session id and backlog straight back to the HTTP handler.
%% The pre-check turns the common refusals into a plain {error, Reason}
%% without starting (and crashing) a process; a racing join that slips past
%% it is still refused by init/1.
start_link(Nick) ->
    case wschat_room:check(Nick) of
        ok ->
            case gen_server:start_link(?MODULE, Nick, []) of
                {ok, Pid} -> {ok, Pid, gen_server:call(Pid, welcome)};
                Other -> Other
            end;
        Err -> Err
    end.

%% Blocks up to poll_timeout for at least one event. Returns {ok, Events}.
recv(Pid) -> gen_server:call(Pid, recv, infinity).
input(Pid, Text) -> gen_server:call(Pid, {input, Text}).
stop(Pid) -> gen_server:call(Pid, stop).

init(Nick) ->
    case wschat_room:join(self(), Nick) of
        {ok, History} ->
            Sid = binary:encode_hex(crypto:strong_rand_bytes(16)),  % 128 random bits
            true = ets:insert_new(wschat_sessions, {Sid, self()}),
            {ok, PT} = application:get_env(wschat, poll_timeout),
            {ok, Idle} = application:get_env(wschat, session_idle),
            S = #st{sid = Sid, welcome = {Sid, History}, poll_timeout = PT, idle = Idle},
            {ok, arm_idle(S)};
        {error, Reason} ->
            {stop, Reason}
    end.

handle_call(welcome, _From, S = #st{welcome = W}) ->
    {reply, W, S#st{welcome = undefined}};
%% Events already waiting: answer straight away.
handle_call(recv, _From, S = #st{buf = [_ | _] = Buf}) ->
    {reply, {ok, lists:reverse(Buf)}, arm_idle(S#st{buf = []})};
%% Nothing yet: park the caller. A second poll on the same session (e.g. a
%% duplicate tab) releases the first with an empty answer.
handle_call(recv, From, S) ->
    S1 = release(S, []),
    T = erlang:send_after(S#st.poll_timeout, self(), poll_timeout),
    {noreply, cancel_idle(S1#st{waiter = From, wait_timer = T})};
handle_call({input, Text}, _From, S) ->
    {reply, wschat_room:input(self(), Text), arm_idle_unless_waiting(S)};
handle_call(stop, _From, S) ->
    {stop, normal, ok, release(S, [])}.

handle_cast(_, S) -> {noreply, S}.

handle_info({chat_event, Ev}, S = #st{waiter = undefined, buf = Buf}) ->
    {noreply, S#st{buf = [Ev | Buf]}};
handle_info({chat_event, Ev}, S) ->
    {noreply, arm_idle(release(S, [Ev]))};
handle_info(poll_timeout, S = #st{waiter = W}) when W =/= undefined ->
    {noreply, arm_idle(release(S, []))};
handle_info(idle_timeout, S = #st{waiter = undefined}) ->
    {stop, normal, S};
handle_info(_, S) ->
    {noreply, S}.

terminate(_Reason, #st{sid = Sid}) ->
    ets:delete(wschat_sessions, Sid).

release(S = #st{waiter = undefined}, _) -> S;
release(S = #st{waiter = W, wait_timer = T}, Events) ->
    erlang:cancel_timer(T),
    gen_server:reply(W, {ok, Events}),
    S#st{waiter = undefined, wait_timer = undefined}.

%% The idle clock only runs while no poll is parked: a client sitting in a
%% 25s poll is very much alive.
arm_idle(S = #st{idle = Idle}) ->
    S1 = cancel_idle(S),
    S1#st{idle_timer = erlang:send_after(Idle, self(), idle_timeout)}.
arm_idle_unless_waiting(S = #st{waiter = undefined}) -> arm_idle(S);
arm_idle_unless_waiting(S) -> S.
cancel_idle(S = #st{idle_timer = undefined}) -> S;
cancel_idle(S = #st{idle_timer = T}) ->
    erlang:cancel_timer(T),
    receive idle_timeout -> ok after 0 -> ok end,  % drop one already in the mailbox
    S#st{idle_timer = undefined}.
