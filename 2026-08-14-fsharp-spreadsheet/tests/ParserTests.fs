module Spreadsheet.Tests.ParserTests

open Spreadsheet
open Spreadsheet.Parser
open Spreadsheet.Tests.TestHelper

let private a1 = { Col = 0; Row = 0 }
let private b2 = { Col = 1; Row = 1 }

let run () =
    assertEqual (ENum 42.0) (parseFormula "42") "bare number"
    assertEqual (ERef a1) (parseFormula "A1") "bare cell reference"

    assertEqual (EBinOp(Add, ENum 1.0, ENum 2.0)) (parseFormula "1+2") "addition"

    // Precedence: * binds tighter than +.
    assertEqual
        (EBinOp(Add, ENum 1.0, EBinOp(Mul, ENum 2.0, ENum 3.0)))
        (parseFormula "1+2*3")
        "multiplication binds tighter than addition"

    // Parens override precedence.
    assertEqual
        (EBinOp(Mul, EBinOp(Add, ENum 1.0, ENum 2.0), ENum 3.0))
        (parseFormula "(1+2)*3")
        "parens override precedence"

    // ^ is right-associative: 2^3^2 = 2^(3^2) = 512, not (2^3)^2 = 64.
    assertEqual
        (EBinOp(Pow, ENum 2.0, EBinOp(Pow, ENum 3.0, ENum 2.0)))
        (parseFormula "2^3^2")
        "exponentiation is right-associative"

    assertEqual (ENeg(ENum 5.0)) (parseFormula "-5") "unary minus"

    assertEqual
        (EBinOp(Sub, ENum 0.0, ENeg(ENum 5.0)))
        (parseFormula "0--5")
        "double minus parses as subtraction of a negation, not decrement"

    assertEqual (ERange(a1, b2)) (parseFormula "A1:B2") "bare range"

    assertEqual
        (EFunc("SUM", [ ERange(a1, b2) ]))
        (parseFormula "SUM(A1:B2)")
        "function call with a range argument"

    assertEqual
        (EFunc("SUM", [ ERef a1; ENum 10.0 ]))
        (parseFormula "SUM(A1, 10)")
        "function call with mixed scalar arguments"

    assertEqual (EFunc("SUM", [])) (parseFormula "SUM()") "function call with no arguments"

    // Function names are case-insensitive on the way in.
    assertEqual (EFunc("SUM", [ ERef a1 ])) (parseFormula "sum(A1)") "lowercase function name"

    assertTrue
        (try
            parseFormula "1 +" |> ignore
            false
         with ParseError _ ->
             true)
        "trailing operator is a parse error"

    assertTrue
        (try
            parseFormula "1 2" |> ignore
            false
         with ParseError _ ->
             true)
        "trailing garbage after a complete expression is a parse error"

    assertTrue
        (try
            parseFormula "(1+2" |> ignore
            false
         with ParseError _ ->
             true)
        "unclosed paren is a parse error"

    assertTrue
        (try
            parseFormula "NOTACELL" |> ignore
            false
         with ParseError _ ->
             true)
        "an identifier that's neither a function call nor a valid cell ref is a parse error"
