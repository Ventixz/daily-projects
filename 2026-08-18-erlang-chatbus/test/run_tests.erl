-module(run_tests).
-export([main/0]).

main() ->
    Mods = [chatbus_room_tests, chatbus_integration_tests],
    Results = [eunit:test(M) || M <- Mods],
    case lists:all(fun(R) -> R =:= ok end, Results) of
        true -> halt(0);
        false -> halt(1)
    end.
