module Spreadsheet.Tests.TestHelper

let mutable Passed = 0
let mutable Failed = 0

let assertEqual (expected: 'a) (actual: 'a) (message: string) =
    if expected = actual then
        Passed <- Passed + 1
    else
        Failed <- Failed + 1
        printfn "FAIL: %s\n  expected: %A\n  actual:   %A" message expected actual

let assertTrue (condition: bool) (message: string) =
    if condition then
        Passed <- Passed + 1
    else
        Failed <- Failed + 1
        printfn "FAIL: %s" message

let assertFalse (condition: bool) (message: string) = assertTrue (not condition) message
