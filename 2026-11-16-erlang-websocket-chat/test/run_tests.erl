-module(run_tests).
-export([main/0]).

main() ->
    Mods = [wschat_ws_tests, wschat_json_tests, wschat_room_tests, wschat_http_tests],
    Results = [eunit:test(M, [verbose]) || M <- Mods],
    halt(case lists:all(fun(R) -> R =:= ok end, Results) of true -> 0; false -> 1 end).
