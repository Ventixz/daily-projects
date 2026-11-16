-module(wschat_ws_tests).
-include_lib("eunit/include/eunit.hrl").
-export([mask/3]).  % shared with wschat_http_tests

%% The worked example from RFC 6455 section 1.3.
accept_key_rfc_example_test() ->
    ?assertEqual(<<"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=">>,
                 wschat_ws:accept_key(<<"dGhlIHNhbXBsZSBub25jZQ==">>)).

%% RFC 6455 section 5.7: a masked "Hello" from the client.
decode_masked_hello_test() ->
    F = <<16#81, 16#85, 16#37, 16#fa, 16#21, 16#3d, 16#7f, 16#9f, 16#4d, 16#51, 16#58>>,
    ?assertEqual({ok, {1, text, <<"Hello">>}, <<>>}, wschat_ws:decode(F, 1000)).

decode_leaves_trailing_bytes_test() ->
    F = mask(1, 1, <<"hi">>),
    ?assertEqual({ok, {1, text, <<"hi">>}, <<"next">>}, wschat_ws:decode(<<F/binary, "next">>, 100)).

partial_frames_need_more_test() ->
    F = mask(1, 1, binary:copy(<<"x">>, 300)),   % 16-bit length form
    [?assertEqual(more, wschat_ws:decode(binary:part(F, 0, N), 1000))
     || N <- [0, 1, 2, 3, 5, 8, byte_size(F) - 1]],
    ?assertMatch({ok, {1, text, _}, <<>>}, wschat_ws:decode(F, 1000)).

sixty_four_bit_length_test() ->
    P = binary:copy(<<"ab">>, 40000),            % 80000 bytes > 65535
    {ok, {1, binary, P2}, <<>>} = wschat_ws:decode(mask(1, 2, P), 100000),
    ?assertEqual(P, P2).

unmasked_client_frame_is_protocol_error_test() ->
    ?assertEqual({error, 1002}, wschat_ws:decode(<<16#81, 2, "hi">>, 100)).

reserved_bits_rejected_test() ->
    <<B0, Rest/binary>> = mask(1, 1, <<"x">>),
    ?assertEqual({error, 1002}, wschat_ws:decode(<<(B0 bor 16#40), Rest/binary>>, 100)).

fragmented_control_frame_rejected_test() ->
    ?assertEqual({error, 1002}, wschat_ws:decode(mask(0, 9, <<>>), 100)).

oversize_rejected_before_payload_arrives_test() ->
    %% Only the header has arrived, claiming 1 MB: refuse now, don't buffer.
    Hdr = <<16#82, (16#80 bor 127), 1048576:64, 1, 2, 3, 4>>,
    ?assertEqual({error, 1009}, wschat_ws:decode(Hdr, 65536)).

encode_length_forms_test() ->
    ?assertMatch(<<16#81, 125, _:125/binary>>, wschat_ws:encode(text, binary:copy(<<"a">>, 125))),
    ?assertMatch(<<16#81, 126, 126:16, _/binary>>, wschat_ws:encode(text, binary:copy(<<"a">>, 126))),
    ?assertMatch(<<16#82, 127, 65536:64, _/binary>>, wschat_ws:encode(binary, binary:copy(<<"a">>, 65536))),
    ?assertEqual(<<16#88, 4, 1000:16, "ok">>, wschat_ws:encode_close(1000, <<"ok">>)).

%% Build a client (masked) frame. Exported for the HTTP tests.
mask(Fin, Op, Payload) ->
    Key = <<K:32>> = crypto:strong_rand_bytes(4),
    Len = byte_size(Payload),
    LenBits = if Len < 126 -> <<1:1, Len:7>>;
                 Len < 65536 -> <<1:1, 126:7, Len:16>>;
                 true -> <<1:1, 127:7, Len:64>>
              end,
    Pad = (4 - Len rem 4) rem 4,
    Words = << <<(W bxor K):32>> || <<W:32>> <= <<Payload/binary, 0:(Pad * 8)>> >>,
    <<Fin:1, 0:3, Op:4, LenBits/bitstring, Key/binary, (binary:part(Words, 0, Len))/binary>>.
