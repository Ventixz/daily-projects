-module(wschat_json_tests).
-include_lib("eunit/include/eunit.hrl").

scalars_test() ->
    ?assertEqual(<<"[1,-2,true,false,null,\"x\"]">>, wschat_json:encode([1, -2, true, false, null, x])).

keys_sorted_test() ->
    ?assertEqual(<<"{\"a\":1,\"b\":[],\"c\":{}}">>, wschat_json:encode(#{c => #{}, a => 1, <<"b">> => []})).

escapes_test() ->
    ?assertEqual(<<"\"q\\\"b\\\\n\\n\\u0001\\u003c/script>\"">>,
                 wschat_json:encode(<<"q\"b\\n\n", 1, "</script>">>)).

utf8_passes_through_test() ->
    ?assertEqual(<<"\"h", 195, 169, "\"">>, wschat_json:encode(<<"h", 195, 169>>)).
