-module(chatbus_room_tests).
-include_lib("eunit/include/eunit.hrl").

join_and_broadcast_test() ->
    {ok, Room} = chatbus_room:start_link(<<"lobby">>),
    ok = chatbus_room:join(Room, self(), "alice"),
    chatbus_room:broadcast(Room, "alice", "hello"),
    receive
        {chat_msg, Room, "alice", "hello"} -> ok
    after 1000 ->
        ?assert(false)
    end,
    gen_server:stop(Room).

join_notifies_existing_members_but_not_the_joiner_test() ->
    {ok, Room} = chatbus_room:start_link(<<"lobby2">>),
    TestPid = self(),
    %% Bystander relays whatever it receives back to the test process, so
    %% we can tell "bob got notified" apart from "carol (the joiner)
    %% didn't" -- both matter, and both need a mailbox of their own.
    Bystander = spawn(fun() -> relay(TestPid) end),
    ok = chatbus_room:join(Room, Bystander, "bob"),
    ok = chatbus_room:join(Room, self(), "carol"),
    receive
        {relayed, {room_event, Room, joined, "carol"}} -> ok
    after 1000 ->
        ?assert(false)
    end,
    %% The joiner (self()) must not receive its own join notification.
    receive
        {room_event, Room, joined, "carol"} -> ?assert(false)
    after 100 ->
        ok
    end,
    gen_server:stop(Room).

relay(TestPid) ->
    receive
        Msg -> TestPid ! {relayed, Msg}, relay(TestPid)
    end.

duplicate_join_is_rejected_test() ->
    {ok, Room} = chatbus_room:start_link(<<"dup">>),
    ok = chatbus_room:join(Room, self(), "dave"),
    ?assertEqual({error, already_joined}, chatbus_room:join(Room, self(), "dave")),
    gen_server:stop(Room).

leave_notifies_remaining_members_test() ->
    {ok, Room} = chatbus_room:start_link(<<"leaveroom">>),
    Bystander = spawn(fun() -> receive after infinity -> ok end end),
    ok = chatbus_room:join(Room, Bystander, "erin"),
    ok = chatbus_room:join(Room, self(), "frank"),
    ok = chatbus_room:leave(Room, self()),
    ?assertEqual(["erin"], chatbus_room:members(Room)),
    gen_server:stop(Room).

%% A member process dying without ever calling leave/2 (crash, kill -9,
%% network drop) still has to be reaped -- that's what the room's
%% monitor/2 call on join is for, not an optional extra.
crashed_member_is_reaped_via_monitor_test() ->
    {ok, Room} = chatbus_room:start_link(<<"crashroom">>),
    Victim = spawn(fun() -> receive stop -> ok end end),
    ok = chatbus_room:join(Room, Victim, "eve"),
    ok = chatbus_room:join(Room, self(), "gina"),
    exit(Victim, kill),
    receive
        {room_event, Room, left, "eve"} -> ok
    after 1000 ->
        ?assert(false)
    end,
    ?assertEqual(["gina"], chatbus_room:members(Room)),
    gen_server:stop(Room).

registry_reuses_existing_room_and_separates_others_test_() ->
    {setup,
     fun() ->
         {ok, SupPid} = chatbus_room_sup:start_link(),
         unlink(SupPid),
         SupPid
     end,
     fun(SupPid) -> exit(SupPid, shutdown) end,
     fun(_SupPid) ->
         [?_test(begin
             {ok, R1} = chatbus_room_sup:get_or_start_room(<<"same">>),
             {ok, R2} = chatbus_room_sup:get_or_start_room(<<"same">>),
             ?assertEqual(R1, R2),
             {ok, R3} = chatbus_room_sup:get_or_start_room(<<"different">>),
             ?assertNotEqual(R1, R3)
         end)]
     end}.
