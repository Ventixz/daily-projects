%% Minimal JSON encoder (OTP 25 has no json module; OTP 27 does).
%% Supports maps with atom/binary keys, lists, binaries (UTF-8), integers,
%% true/false/null. Map keys are sorted so output is deterministic.
-module(wschat_json).
-export([encode/1]).

encode(Term) -> iolist_to_binary(enc(Term)).

enc(true) -> <<"true">>;
enc(false) -> <<"false">>;
enc(null) -> <<"null">>;
enc(A) when is_atom(A) -> enc(atom_to_binary(A, utf8));
enc(I) when is_integer(I) -> integer_to_binary(I);
enc(B) when is_binary(B) -> [$", escape(B), $"];
enc(M) when is_map(M) ->
    %% Sort after converting keys: atoms sort before all binaries in
    %% Erlang term order, so sorting raw keys would mix two orders.
    Pairs = [[enc(K), $:, enc(V)] || {K, V} <- lists:keysort(1, [{key(K), V} || {K, V} <- maps:to_list(M)])],
    [${, lists:join($,, Pairs), $}];
enc(L) when is_list(L) -> [$[, lists:join($,, [enc(X) || X <- L]), $]].

key(K) when is_atom(K) -> atom_to_binary(K, utf8);
key(K) when is_binary(K) -> K.

escape(B) -> [esc(C) || <<C>> <= B].

esc($") -> <<"\\\"">>;
esc($\\) -> <<"\\\\">>;
esc($\n) -> <<"\\n">>;
esc($\r) -> <<"\\r">>;
esc($\t) -> <<"\\t">>;
%% "</" inside a JSON string is legal but could close a <script> tag if
%% the payload were ever inlined into HTML; escaping "<" costs nothing.
esc($<) -> <<"\\u003c">>;
esc(C) when C < 16#20 -> io_lib:format("\\u~4.16.0b", [C]);
esc(C) -> C.  % bytes >= 0x80 are UTF-8 continuation; pass through
