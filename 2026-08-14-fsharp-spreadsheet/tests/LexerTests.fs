module Spreadsheet.Tests.LexerTests

open Spreadsheet.Lexer
open Spreadsheet.Tests.TestHelper

let run () =
    assertEqual [ TNum 1.0; TPlus; TNum 2.0; TEnd ] (tokenize "1+2") "simple addition"

    assertEqual
        [ TIdent "SUM"; TLParen; TIdent "A1"; TColon; TIdent "A3"; TRParen; TEnd ]
        (tokenize "SUM(A1:A3)")
        "function call with a range"

    assertEqual [ TNum 3.5; TEnd ] (tokenize "3.5") "decimal number"
    assertEqual [ TStr "hi there"; TEnd ] (tokenize "\"hi there\"") "string literal"

    assertEqual
        [ TLParen; TMinus; TIdent "A1"; TRParen; TCaret; TNum 2.0; TEnd ]
        (tokenize "(-A1)^2")
        "parens, unary minus, caret"

    assertEqual [ TIdent "A1"; TComma; TIdent "B1"; TEnd ] (tokenize "A1, B1") "whitespace around comma is skipped"

    assertTrue
        (try
            tokenize "\"unterminated" |> ignore
            false
         with LexError _ ->
             true)
        "unterminated string raises LexError"

    assertTrue
        (try
            tokenize "1 + @" |> ignore
            false
         with LexError _ ->
             true)
        "unexpected character raises LexError"
