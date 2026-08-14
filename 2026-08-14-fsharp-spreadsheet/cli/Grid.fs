module Spreadsheet.Cli.Grid

open Spreadsheet
open Spreadsheet.Sheet

/// Loads a `.sheet` file: one cell per line, `<ref>=<raw content>`. Blank
/// lines and lines starting with '#' are ignored. Returns the refs that
/// failed to load, each with the reason, rather than throwing -- one bad
/// line in a demo file shouldn't take down the whole load.
let loadFile (path: string) (sheet: Sheet) : (string * string) list =
    let errors = ResizeArray<string * string>()

    for line in System.IO.File.ReadAllLines path do
        let line = line.TrimEnd()

        if line <> "" && not (line.StartsWith "#") then
            let eq = line.IndexOf '='

            if eq < 0 then
                errors.Add(line, "expected '<ref>=<content>'")
            else
                let refText = line.Substring(0, eq).Trim()
                let raw = line.Substring(eq + 1)

                match ColumnCodec.tryParseCellRef refText with
                | None -> errors.Add(line, sprintf "'%s' is not a valid cell reference" refText)
                | Some r ->
                    match sheet.SetCell(r, raw) with
                    | Ok() -> ()
                    | Error msg -> errors.Add(line, msg)

    List.ofSeq errors

/// Renders the sheet's populated extent as an aligned text grid, column
/// letters and row numbers included -- close enough to what a terminal
/// spreadsheet viewer would show to make the dependency-driven recalc
/// visible when a value changes between two prints.
let render (sheet: Sheet) : string =
    match sheet.Extent with
    | None -> "(empty sheet)"
    | Some(topLeft, bottomRight) ->
        let cellText (r: CellRef) = sheet.GetValue r |> CellValue.toDisplayString

        let colWidth (col: int) =
            [ topLeft.Row .. bottomRight.Row ]
            |> List.map (fun row -> (cellText { Col = col; Row = row }).Length)
            |> fun lens -> List.max (ColumnCodec.colToLetters(col).Length :: lens)

        let cols = [ topLeft.Col .. bottomRight.Col ]
        let widths = cols |> List.map colWidth
        let rowLabelWidth = string (bottomRight.Row + 1) |> String.length

        let pad (s: string) (w: int) = s.PadLeft w

        let sb = System.Text.StringBuilder()
        sb.Append(String.replicate rowLabelWidth " ") |> ignore

        for col, w in List.zip cols widths do
            sb.Append(" | ").Append(pad (ColumnCodec.colToLetters col) w) |> ignore

        sb.Append('\n') |> ignore

        for row in topLeft.Row .. bottomRight.Row do
            sb.Append(pad (string (row + 1)) rowLabelWidth) |> ignore

            for col, w in List.zip cols widths do
                sb.Append(" | ").Append(pad (cellText { Col = col; Row = row }) w) |> ignore

            sb.Append('\n') |> ignore

        sb.ToString()
