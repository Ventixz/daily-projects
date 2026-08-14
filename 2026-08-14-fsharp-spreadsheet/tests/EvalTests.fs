module Spreadsheet.Tests.EvalTests

open Spreadsheet
open Spreadsheet.Parser
open Spreadsheet.Eval
open Spreadsheet.Tests.TestHelper

/// A fixed lookup table standing in for "already-computed neighboring
/// cells" -- Eval never needs a real Sheet, just something CellRef -> CellValue.
let private lookupOf (pairs: (string * CellValue) list) : CellRef -> CellValue =
    let table =
        pairs
        |> List.map (fun (refText, v) -> (ColumnCodec.tryParseCellRef refText).Value, v)
        |> Map.ofList

    fun r -> Map.tryFind r table |> Option.defaultValue VEmpty

let private evalStr (lookup: CellRef -> CellValue) (formula: string) : CellValue =
    evalExpr lookup (parseFormula formula)

let run () =
    let empty = lookupOf []

    assertEqual (VNumber 5.0) (evalStr empty "2+3") "addition"
    assertEqual (VNumber 6.0) (evalStr empty "2*3") "multiplication"
    assertEqual (VNumber 8.0) (evalStr empty "2^3") "exponentiation"
    assertEqual (VNumber -2.0) (evalStr empty "-2") "unary minus"

    assertEqual (VError "#DIV/0!") (evalStr empty "1/0") "division by zero is an error, not infinity"

    let withRefs = lookupOf [ "A1", VNumber 10.0; "A2", VNumber 3.0 ]
    assertEqual (VNumber 13.0) (evalStr withRefs "A1+A2") "arithmetic across cell references"

    // Referencing a blank cell reads as 0 in arithmetic -- same convention
    // Excel uses, and the reason `=A1+1` on a fresh sheet is 1, not an error.
    assertEqual (VNumber 1.0) (evalStr empty "A1+1") "blank cell reads as 0 in arithmetic"

    // Text can't be added -- it's a #VALUE! error, not silently 0 or a
    // string concatenation this engine doesn't support.
    let withText = lookupOf [ "A1", VText "hello" ]
    assertEqual (VError "#VALUE!") (evalStr withText "A1+1") "text operand is a #VALUE! error"

    // An error cell poisons anything that reads it.
    let withErr = lookupOf [ "A1", VError "#DIV/0!" ]
    assertEqual (VError "#DIV/0!") (evalStr withErr "A1+1") "error propagates through arithmetic"
    assertEqual (VError "#DIV/0!") (evalStr withErr "SUM(A1:A1)") "error propagates through SUM's range"

    // SUM/AVERAGE/MIN/MAX ignore blanks and text within a *range*, the way
    // a spreadsheet SUM skips a header cell mixed into the range by accident.
    let mixedRange = lookupOf [ "A1", VNumber 10.0; "A2", VText "header"; "A3", VEmpty; "A4", VNumber 20.0 ]
    assertEqual (VNumber 30.0) (evalStr mixedRange "SUM(A1:A4)") "SUM skips text and blanks in a range"
    assertEqual (VNumber 15.0) (evalStr mixedRange "AVERAGE(A1:A4)") "AVERAGE divides by the count of numbers found, not the range size"
    assertEqual (VNumber 2.0) (evalStr mixedRange "COUNT(A1:A4)") "COUNT counts only the numeric cells"

    assertEqual (VError "#DIV/0!") (evalStr empty "AVERAGE(A1:A1)") "AVERAGE of an all-blank range is a division by zero"

    // A bare scalar argument is stricter than a range: text there is
    // always an error, it isn't silently skipped the way it would be
    // inside a range.
    assertEqual (VError "#VALUE!") (evalStr withText "SUM(A1, 5)") "SUM of a bare text argument is #VALUE!, unlike a text cell inside a range"

    assertEqual (VError "#NAME?") (evalStr empty "NOSUCHFUNC(1)") "unknown function name is #NAME?"

    assertEqual (VError "#VALUE!") (evalStr empty "A1:A2") "a bare range used where a scalar is expected is #VALUE!"
