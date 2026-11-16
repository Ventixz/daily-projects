%% RFC 6455 WebSocket: the opening handshake key and frame codec.
%% Pure functions only -- no sockets -- so every edge case is unit-testable.
-module(wschat_ws).
-export([accept_key/1, decode/2, encode/2, encode_close/2]).

-define(GUID, <<"258EAFA5-E914-47DA-95CA-C5AB0DC85B11">>).

%% Sec-WebSocket-Accept = base64(sha1(key ++ magic GUID)). The GUID proves
%% the server actually speaks WebSocket rather than echoing headers.
accept_key(ClientKey) ->
    base64:encode(crypto:hash(sha, <<ClientKey/binary, ?GUID/binary>>)).

%% decode(Buffer, MaxPayload) ->
%%   {ok, {Fin, Opcode, Payload}, Rest} | more | {error, Code}
%% Frames from a client MUST be masked (section 5.1); an unmasked one is a
%% protocol error (close code 1002).
decode(<<Fin:1, Rsv:3, Op:4, Mask:1, Len7:7, Rest/binary>>, Max) ->
    if
        Rsv =/= 0 -> {error, 1002};            % no extensions negotiated
        Mask =/= 1 -> {error, 1002};
        Op >= 8, (Fin =:= 0 orelse Len7 > 125) -> {error, 1002}; % 5.5
        true -> decode_len(Fin, Op, Len7, Rest, Max)
    end;
decode(_, _) -> more.

decode_len(Fin, Op, 126, <<Len:16, Rest/binary>>, Max) -> payload(Fin, Op, Len, Rest, Max);
decode_len(_, _, 127, <<1:1, _:63, _/binary>>, _) -> {error, 1002}; % MSB must be 0
decode_len(Fin, Op, 127, <<Len:64, Rest/binary>>, Max) -> payload(Fin, Op, Len, Rest, Max);
decode_len(Fin, Op, Len, Rest, Max) when Len < 126 -> payload(Fin, Op, Len, Rest, Max);
decode_len(_, _, _, _, _) -> more.

payload(_, _, Len, _, Max) when Len > Max -> {error, 1009}; % reject before buffering
payload(Fin, Op, Len, Bin, _) ->
    case Bin of
        <<Key:4/binary, Masked:Len/binary, Rest/binary>> ->
            {ok, {Fin, op(Op), unmask(Masked, Key)}, Rest};
        _ -> more
    end.

op(0) -> continuation;
op(1) -> text;
op(2) -> binary;
op(8) -> close;
op(9) -> ping;
op(10) -> pong;
op(_) -> reserved.

%% XOR each byte with key[i mod 4]. Do it 4 bytes at a time, then the tail.
unmask(Data, <<K:32>> = Key) -> unmask(Data, K, Key, <<>>).
unmask(<<W:32, Rest/binary>>, K, Key, Acc) -> unmask(Rest, K, Key, <<Acc/binary, (W bxor K):32>>);
unmask(Tail, _, Key, Acc) ->
    Bits = byte_size(Tail) * 8,
    <<KT:Bits, _/bitstring>> = Key,
    <<T:Bits>> = Tail,
    <<Acc/binary, (T bxor KT):Bits>>.

%% Server-to-client frames are never masked. Always FIN=1: we don't fragment.
encode(Type, Payload) ->
    Op = case Type of text -> 1; binary -> 2; close -> 8; ping -> 9; pong -> 10 end,
    Len = byte_size(Payload),
    Header = if
                 Len < 126 -> <<1:1, 0:3, Op:4, 0:1, Len:7>>;
                 Len < 65536 -> <<1:1, 0:3, Op:4, 0:1, 126:7, Len:16>>;
                 true -> <<1:1, 0:3, Op:4, 0:1, 127:7, Len:64>>
             end,
    <<Header/binary, Payload/binary>>.

encode_close(Code, Reason) -> encode(close, <<Code:16, Reason/binary>>).
