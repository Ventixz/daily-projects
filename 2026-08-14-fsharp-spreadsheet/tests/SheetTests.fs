module Spreadsheet.Tests.SheetTests

open Spreadsheet
open Spreadsheet.Sheet
open Spreadsheet.Tests.TestHelper

let private r (text: string) : CellRef = (ColumnCodec.tryParseCellRef text).Value

let private set (sheet: Sheet) (refText: string) (raw: string) =
    match sheet.SetCell(r refText, raw) with
    | Ok() -> ()
    | Error msg -> failwithf "unexpected parse error setting %s = %s: %s" refText raw msg

let run () =
    // --- basic content classification -----------------------------------
    let s1 = Sheet()
    set s1 "A1" "42"
    assertEqual (VNumber 42.0) (s1.GetValue(r "A1")) "a bare number literal is a number"

    set s1 "A2" "hello"
    assertEqual (VText "hello") (s1.GetValue(r "A2")) "non-numeric bare text is a text value"

    set s1 "A3" "=A1+1"
    assertEqual (VNumber 43.0) (s1.GetValue(r "A3")) "a formula computes off other cells"

    assertEqual VEmpty (s1.GetValue(r "Z9")) "an untouched cell reads as blank"

    // --- diamond dependency: shared cell evaluated once per edit ---------
    //        A1
    //       /  \
    //      B1   C1
    //       \  /
    //        D1
    let s2 = Sheet()
    set s2 "A1" "5"
    set s2 "B1" "=A1*2"
    set s2 "C1" "=A1+1"
    set s2 "D1" "=B1+C1"
    set s2 "E1" "100" // unrelated cell, should never be touched by edits to A1

    assertEqual (VNumber 16.0) (s2.GetValue(r "D1")) "D1 = B1(10) + C1(6) = 16"

    let evalsBeforeEdit = s2.EvalCount
    set s2 "A1" "10"
    let evalsForEdit = s2.EvalCount - evalsBeforeEdit

    assertEqual (VNumber 31.0) (s2.GetValue(r "D1")) "D1 recomputed correctly after A1 changes: B1(20) + C1(11) = 31"
    assertEqual
        4
        evalsForEdit
        "editing A1 recomputes exactly {A1, B1, C1, D1} -- D1 once, not twice, despite two paths to it, and E1 not at all"

    // --- edit that drops a dependency stops recomputing on it later ------
    set s2 "B1" "999" // B1 no longer reads A1 at all
    let evalsBeforeSecondEdit = s2.EvalCount
    set s2 "A1" "20"
    let evalsForSecondEdit = s2.EvalCount - evalsBeforeSecondEdit

    assertEqual (VNumber 999.0) (s2.GetValue(r "B1")) "B1 keeps its now-independent value"
    assertEqual
        3
        evalsForSecondEdit
        "after B1 stops depending on A1, editing A1 only recomputes {A1, C1, D1} -- B1's stale dependency edge was cleaned up"

    // --- cycles ------------------------------------------------------------
    let s3 = Sheet()
    set s3 "X1" "=Y1+1"
    set s3 "Y1" "=X1+1"
    assertEqual (VError "#CYCLE!") (s3.GetValue(r "X1")) "direct two-cell cycle: X1"
    assertEqual (VError "#CYCLE!") (s3.GetValue(r "Y1")) "direct two-cell cycle: Y1"

    set s3 "W1" "=X1+Y1"
    assertEqual (VError "#CYCLE!") (s3.GetValue(r "W1")) "a cell merely reading from a cycle is unorderable too"

    let s4 = Sheet()
    set s4 "Z1" "=Z1+1"
    assertEqual (VError "#CYCLE!") (s4.GetValue(r "Z1")) "self-reference is a 1-cell cycle"

    // Breaking the cycle should let it evaluate normally again.
    set s3 "Y1" "5"
    assertEqual (VNumber 6.0) (s3.GetValue(r "X1")) "X1 = Y1 + 1 once the cycle is broken"
    assertEqual (VNumber 11.0) (s3.GetValue(r "W1")) "W1 recovers too, once its inputs are no longer cyclic"

    // --- malformed input leaves the old value in place --------------------
    let s5 = Sheet()
    set s5 "A1" "=1+1"
    let before = s5.GetValue(r "A1")
    let result = s5.SetCell(r "A1", "=1+")
    assertTrue (Result.isError result) "a malformed formula is rejected"
    assertEqual before (s5.GetValue(r "A1")) "a rejected edit leaves the previous value untouched"

    // --- SUM over a range that includes the formula's own sheet neighbors -
    let s6 = Sheet()
    set s6 "A1" "10"
    set s6 "A2" "20"
    set s6 "A3" "30"
    set s6 "A4" "=SUM(A1:A3)"
    assertEqual (VNumber 60.0) (s6.GetValue(r "A4")) "SUM over a range of plain numbers"

    let evalsBeforeA1Edit = s6.EvalCount
    set s6 "A1" "15"
    assertEqual (VNumber 65.0) (s6.GetValue(r "A4")) "SUM picks up a change to a cell inside its range"
    assertEqual 2 (s6.EvalCount - evalsBeforeA1Edit) "only A1 and A4 recompute -- A2 and A3 are untouched by the edit"
