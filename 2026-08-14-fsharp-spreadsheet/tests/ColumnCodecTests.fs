module Spreadsheet.Tests.ColumnCodecTests

open Spreadsheet
open Spreadsheet.Tests.TestHelper

let run () =
    // Known mappings, including the boundary that trips up a naive
    // (non-bijective) base-26 implementation: going from a single letter
    // to a double letter at Z -> AA, and the second boundary at AZ -> BA.
    let knownPairs =
        [ 0, "A"
          1, "B"
          25, "Z"
          26, "AA"
          27, "AB"
          51, "AZ"
          52, "BA"
          701, "ZZ"
          702, "AAA" ]

    for col, letters in knownPairs do
        assertEqual letters (ColumnCodec.colToLetters col) (sprintf "colToLetters %d" col)
        assertEqual (Some col) (ColumnCodec.lettersToCol letters) (sprintf "lettersToCol %s" letters)

    // Round trip every column in a wide range, including across both
    // single/double and double/triple letter boundaries.
    for col in 0..1500 do
        let letters = ColumnCodec.colToLetters col
        assertEqual (Some col) (ColumnCodec.lettersToCol letters) (sprintf "round trip col %d via '%s'" col letters)

    assertEqual None (ColumnCodec.lettersToCol "") "empty string is not a column"
    assertEqual None (ColumnCodec.lettersToCol "1A") "digit-prefixed text is not a column"
    assertEqual None (ColumnCodec.lettersToCol "a") "lettersToCol requires uppercase input"

    // Cell-reference parsing.
    assertEqual (Some { Col = 0; Row = 0 }) (ColumnCodec.tryParseCellRef "A1") "parse A1"
    assertEqual (Some { Col = 0; Row = 0 }) (ColumnCodec.tryParseCellRef "a1") "parse is case-insensitive"
    assertEqual (Some { Col = 26; Row = 99 }) (ColumnCodec.tryParseCellRef "AA100") "parse AA100"
    assertEqual None (ColumnCodec.tryParseCellRef "A0") "row 0 does not exist (rows are 1-based on the surface)"
    assertEqual None (ColumnCodec.tryParseCellRef "1A") "digits before letters is not a valid ref"
    assertEqual None (ColumnCodec.tryParseCellRef "A1B2") "trailing letters after digits is not a valid ref"
    assertEqual None (ColumnCodec.tryParseCellRef "") "empty string is not a valid ref"

    assertEqual "A1" (ColumnCodec.cellRefToString { Col = 0; Row = 0 }) "cellRefToString A1"
    assertEqual "AA100" (ColumnCodec.cellRefToString { Col = 26; Row = 99 }) "cellRefToString AA100"
