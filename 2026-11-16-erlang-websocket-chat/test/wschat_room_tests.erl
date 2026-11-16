-module(wschat_room_tests).
-include_lib("eunit/include/eunit.hrl").

room_test_() ->
    {foreach, fun setup/0, fun cleanup/1,
     [fun join_broadcasts_to_everyone/0,
      fun nick_rules/0,
      fun say_rules/0,
      fun rename_broadcasts_old_and_new/0,
      fun crashed_member_leaves/0,
      fun history_is_bounded_and_replayed/0]}.

setup() ->
    application:load(wschat),
    application:set_env(wschat, history_size, 3),
    {ok, Pid} = wschat_room:start_link(),
    unlink(Pid),
    Pid.

cleanup(Pid) ->
    Ref = monitor(process, Pid),
    exit(Pid, kill),
    receive {'DOWN', Ref, _, _, _} -> ok end,
    application:set_env(wschat, history_size, 50).

%% A fake member: a process that joins and forwards everything it gets.
member(Nick) ->
    Me = self(),
    P = spawn(fun() ->
                      Me ! {self(), wschat_room:join(self(), Nick)},
                      fwd(Me)
              end),
    receive {P, R} -> {P, R} end.
fwd(To) ->
    receive
        {do, F} -> To ! {self(), result, F()};
        M -> To ! {self(), M}
    end,
    fwd(To).
%% Run F inside the member process (so self() is the member) and return its result.
as(P, F) -> P ! {do, F}, receive {P, result, R} -> R after 1000 -> timeout end.
next(P) -> receive {P, {chat_event, E}} -> E after 1000 -> timeout end.

join_broadcasts_to_everyone() ->
    {A, {ok, []}} = member(<<"ada">>),
    ?assertMatch(#{type := join, nick := <<"ada">>, id := 1}, next(A)),
    {B, {ok, [#{type := join}]}} = member(<<"bob">>),
    ?assertMatch(#{type := join, nick := <<"bob">>}, next(A)),
    ?assertMatch(#{type := join, nick := <<"bob">>}, next(B)),
    ?assertEqual([<<"ada">>, <<"bob">>], wschat_room:members()).

nick_rules() ->
    {_, {ok, _}} = member(<<"Ada">>),
    ?assertMatch({_, {error, nick_taken}}, member(<<"ada">>)),   % case-insensitive
    [?assertMatch({_, {error, invalid_nick}}, member(N))
     || N <- [<<>>, <<"has space">>, <<"semi;colon">>, binary:copy(<<"a">>, 21), <<"h", 195, 169>>]],
    ?assertMatch({_, {ok, _}}, member(binary:copy(<<"a">>, 20))).

say_rules() ->
    {A, {ok, _}} = member(<<"ada">>),
    _ = next(A),
    ?assertEqual(ok, as(A, fun() -> wschat_room:say(self(), <<"  hi  ">>) end)),
    ?assertMatch(#{type := msg, nick := <<"ada">>, text := <<"hi">>}, next(A)),
    ?assertEqual({error, empty}, as(A, fun() -> wschat_room:say(self(), <<"   ">>) end)),
    ?assertEqual({error, too_long}, as(A, fun() -> wschat_room:say(self(), binary:copy(<<"x">>, 2001)) end)),
    ?assertEqual({error, not_joined}, wschat_room:say(self(), <<"hi">>)).

rename_broadcasts_old_and_new() ->
    {A, {ok, _}} = member(<<"ada">>),
    {B, {ok, _}} = member(<<"bob">>),
    _ = next(A), _ = next(A), _ = next(B),
    ?assertEqual({error, nick_taken}, as(A, fun() -> wschat_room:input(self(), <<"/nick BOB">>) end)),
    ?assertEqual(ok, as(A, fun() -> wschat_room:input(self(), <<"/nick ada_l ">>) end)),
    ?assertMatch(#{type := nick, nick := <<"ada_l">>, old := <<"ada">>}, next(B)),
    ?assertEqual([<<"ada_l">>, <<"bob">>], wschat_room:members()).

crashed_member_leaves() ->
    {A, {ok, _}} = member(<<"ada">>),
    {B, {ok, _}} = member(<<"bob">>),
    _ = next(A), _ = next(A), _ = next(B),
    exit(B, kill),
    ?assertMatch(#{type := leave, nick := <<"bob">>}, next(A)),
    ?assertEqual([<<"ada">>], wschat_room:members()),
    %% The nick is free again.
    ?assertMatch({_, {ok, _}}, member(<<"bob">>)).

history_is_bounded_and_replayed() ->
    {A, {ok, _}} = member(<<"ada">>),
    [ok = as(A, fun() -> wschat_room:say(self(), integer_to_binary(N)) end) || N <- [1, 2, 3, 4]],
    {_, {ok, Hist}} = member(<<"late">>),
    ?assertEqual([<<"2">>, <<"3">>, <<"4">>], [T || #{text := T} <- Hist]),
    Ids = [I || #{id := I} <- Hist],
    ?assertEqual(lists:sort(Ids), Ids).
