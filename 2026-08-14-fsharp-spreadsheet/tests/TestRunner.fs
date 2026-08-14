module Spreadsheet.Tests.TestRunner

open Spreadsheet.Tests

[<EntryPoint>]
let main _ =
    ColumnCodecTests.run ()
    LexerTests.run ()
    ParserTests.run ()
    EvalTests.run ()
    GraphTests.run ()
    SheetTests.run ()

    printfn "\n%d passed, %d failed" TestHelper.Passed TestHelper.Failed
    if TestHelper.Failed > 0 then 1 else 0
