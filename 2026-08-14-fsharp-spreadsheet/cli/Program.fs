module Spreadsheet.Cli.Program

open Spreadsheet
open Spreadsheet.Sheet
open Spreadsheet.Cli.Grid

let private runRepl (sheet: Sheet) =
    printfn "F# spreadsheet REPL. Commands: set <ref> <content> | get <ref> | print | quit"

    let mutable running = true

    while running do
        printf "> "
        match System.Console.In.ReadLine() with
        | null ->
            running <- false
        | line ->
            let line = line.Trim()

            if line = "" then
                ()
            elif line = "quit" || line = "exit" then
                running <- false
            elif line = "print" then
                printf "%s" (render sheet)
            elif line.StartsWith "get " then
                let refText = line.Substring(4).Trim()

                match ColumnCodec.tryParseCellRef refText with
                | None -> printfn "'%s' is not a valid cell reference" refText
                | Some r -> printfn "%s = %s" refText (CellValue.toDisplayString (sheet.GetValue r))
            elif line.StartsWith "set " then
                let rest = line.Substring(4).Trim()
                let sp = rest.IndexOf ' '

                if sp < 0 then
                    printfn "usage: set <ref> <content>"
                else
                    let refText = rest.Substring(0, sp)
                    let content = rest.Substring(sp + 1)

                    match ColumnCodec.tryParseCellRef refText with
                    | None -> printfn "'%s' is not a valid cell reference" refText
                    | Some r ->
                        match sheet.SetCell(r, content) with
                        | Ok() -> printfn "%s = %s" refText (CellValue.toDisplayString (sheet.GetValue r))
                        | Error msg -> printfn "error: %s" msg
            else
                printfn "unrecognized command: %s" line

[<EntryPoint>]
let main argv =
    if Array.contains "--repl" argv then
        runRepl (Sheet())
        0
    else
        let path =
            match argv |> Array.tryFind (fun a -> not (a.StartsWith "--")) with
            | Some p -> p
            | None -> System.IO.Path.Combine(__SOURCE_DIRECTORY__, "..", "examples", "budget.sheet")

        let sheet = Sheet()
        let errors = loadFile path sheet

        for line, msg in errors do
            eprintfn "skipped '%s': %s" line msg

        printfn "Loaded %s\n" path
        printfn "%s" (render sheet)

        // Demonstrate the dependency-driven recalc: bump Q1 revenue and
        // show that only the cells downstream of it re-evaluate.
        match ColumnCodec.tryParseCellRef "B2" with
        | Some editRef when sheet.GetContent editRef <> Empty ->
            let before = sheet.EvalCount
            printfn "--- editing %s, recalculating dependents only ---\n" (ColumnCodec.cellRefToString editRef)
            let raw = sheet.GetRaw editRef

            match System.Double.TryParse(raw, System.Globalization.CultureInfo.InvariantCulture) with
            | true, n ->
                sheet.SetCell(editRef, string (n + 500.0)) |> ignore
                printfn "%s" (render sheet)
                printfn "cells recomputed by that one edit: %d" (sheet.EvalCount - before)
            | false, _ -> ()
        | _ -> ()

        0
