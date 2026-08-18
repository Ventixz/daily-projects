%% AI-written implementation, built for the daily-projects practice repo.
-module(chatbus_client).
-behaviour(gen_server).

-export([start_link/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

-record(state, {socket, nick, room}).

start_link(Socket) ->
    gen_server:start_link(?MODULE, Socket, []).

init(Socket) ->
    {ok, #state{socket = Socket}}.

%% Sent by chatbus_listener only after gen_tcp:controlling_process/2 has
%% already handed this socket to us -- setting {active, once} before that
%% handoff completes would arm a socket we don't yet own the messages for.
handle_cast(activate, State = #state{socket = Socket}) ->
    inet:setopts(Socket, [{active, once}]),
    send_line(Socket, "* welcome to chatbus. set a nick: NICK <name>"),
    {noreply, State};
handle_cast(_Msg, State) ->
    {noreply, State}.

handle_call(_Req, _From, State) ->
    {reply, ok, State}.

handle_info({tcp, Socket, Data}, State = #state{socket = Socket}) ->
    inet:setopts(Socket, [{active, once}]),
    Line = string:trim(binary_to_list(Data)),
    {noreply, handle_line(Line, State)};
handle_info({tcp_closed, Socket}, State = #state{socket = Socket}) ->
    {stop, normal, leave_room(State)};
handle_info({tcp_error, Socket, _Reason}, State = #state{socket = Socket}) ->
    {stop, normal, leave_room(State)};
handle_info({chat_msg, _RoomPid, FromNick, Text}, State = #state{socket = Socket}) ->
    send_line(Socket, io_lib:format("~s: ~s", [FromNick, Text])),
    {noreply, State};
handle_info({room_event, _RoomPid, joined, Nick}, State = #state{socket = Socket}) ->
    send_line(Socket, io_lib:format("* ~s joined", [Nick])),
    {noreply, State};
handle_info({room_event, _RoomPid, left, Nick}, State = #state{socket = Socket}) ->
    send_line(Socket, io_lib:format("* ~s left", [Nick])),
    {noreply, State};
handle_info(_Msg, State) ->
    {noreply, State}.

terminate(_Reason, State) ->
    leave_room(State),
    ok.

code_change(_OldVsn, State, _Extra) ->
    {ok, State}.

%% Internal

handle_line("NICK " ++ Name, State = #state{socket = Socket}) when Name =/= [] ->
    send_line(Socket, "* nick set to " ++ Name),
    State#state{nick = Name};
handle_line("JOIN " ++ _Room, State = #state{nick = undefined, socket = Socket}) ->
    send_line(Socket, "! set a nick first: NICK <name>"),
    State;
handle_line("JOIN " ++ Room, State = #state{socket = Socket, nick = Nick}) when Room =/= [] ->
    State1 = leave_room(State),
    {ok, RoomPid} = chatbus_room_sup:get_or_start_room(Room),
    ok = chatbus_room:join(RoomPid, self(), Nick),
    send_line(Socket, "* joined " ++ Room),
    State1#state{room = RoomPid};
handle_line("MSG " ++ _Text, State = #state{room = undefined, socket = Socket}) ->
    send_line(Socket, "! join a room first: JOIN <room>"),
    State;
handle_line("MSG " ++ Text, State = #state{room = RoomPid, nick = Nick}) ->
    chatbus_room:broadcast(RoomPid, Nick, Text),
    State;
handle_line("LEAVE", State) ->
    leave_room(State);
handle_line("QUIT", State = #state{socket = Socket}) ->
    send_line(Socket, "* bye"),
    State1 = leave_room(State),
    gen_tcp:close(Socket),
    State1;
handle_line(_Other, State = #state{socket = Socket}) ->
    send_line(Socket, "! unknown command"),
    State.

leave_room(State = #state{room = undefined}) ->
    State;
leave_room(State = #state{room = RoomPid}) ->
    catch chatbus_room:leave(RoomPid, self()),
    State#state{room = undefined}.

send_line(Socket, IoData) ->
    gen_tcp:send(Socket, [IoData, "\n"]).
