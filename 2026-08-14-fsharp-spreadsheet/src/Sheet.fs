module Spreadsheet.Sheet

type private CellRecord =
    { mutable Raw: string
      mutable Content: CellContent
      mutable Value: CellValue
      mutable Deps: Set<CellRef>
      mutable Dependents: Set<CellRef> }

let private emptyRecord () =
    { Raw = ""
      Content = Empty
      Value = VEmpty
      Deps = Set.empty
      Dependents = Set.empty }

/// A spreadsheet: cells keyed by reference, evaluated lazily-on-write and
/// kept consistent by recomputing only what an edit could have changed.
type Sheet() =
    let cells = System.Collections.Generic.Dictionary<CellRef, CellRecord>()

    /// Total number of individual cell (re)evaluations performed, across
    /// the sheet's lifetime. Exists so tests can prove that an edit only
    /// recomputes its dependents, and that a diamond-shaped dependency
    /// graph evaluates each shared cell once, not once per path to it.
    member val EvalCount = 0 with get, set

    member private this.GetOrCreate(r: CellRef) : CellRecord =
        match cells.TryGetValue r with
        | true, rec' -> rec'
        | false, _ ->
            let rec' = emptyRecord ()
            cells.[r] <- rec'
            rec'

    member this.GetContent(r: CellRef) : CellContent =
        match cells.TryGetValue r with
        | true, rec' -> rec'.Content
        | false, _ -> Empty

    member this.GetRaw(r: CellRef) : string =
        match cells.TryGetValue r with
        | true, rec' -> rec'.Raw
        | false, _ -> ""

    member this.GetValue(r: CellRef) : CellValue =
        match cells.TryGetValue r with
        | true, rec' -> rec'.Value
        | false, _ -> VEmpty

    /// Every cell that has ever held content or been referenced -- the
    /// footprint used to size a printed grid.
    member this.KnownCells : CellRef seq = cells.Keys :> CellRef seq

    member private this.Recalculate(start: CellRef) =
        let affected = Graph.affectedSet (fun c -> (this.GetOrCreate c).Dependents) start
        let result = Graph.topoOrder (fun c -> (this.GetOrCreate c).Deps) affected

        for c in result.Order do
            let rec' = this.GetOrCreate c
            this.EvalCount <- this.EvalCount + 1
            rec'.Value <- Eval.evalContent this.GetValue rec'.Content

        for c in result.Cycle do
            let rec' = this.GetOrCreate c
            rec'.Value <- VError "#CYCLE!"

    /// Sets a cell's raw text, reparses it, and recomputes exactly the
    /// cells whose value could have changed as a result -- this cell and
    /// everything (transitively) that depends on it. Returns an error
    /// (leaving the cell's previous content untouched) if the text doesn't
    /// parse as a formula.
    member this.SetCell(r: CellRef, raw: string) : Result<unit, string> =
        let parsed =
            try
                Ok(Parser.parseContent raw)
            with
            | Parser.ParseError msg -> Error msg
            | Lexer.LexError msg -> Error msg

        match parsed with
        | Error msg -> Error msg
        | Ok content ->
            let rec' = this.GetOrCreate r
            let oldDeps = rec'.Deps
            let newDeps = Deps.contentDependencies content

            for d in Set.difference oldDeps newDeps do
                let dRec = this.GetOrCreate d
                dRec.Dependents <- Set.remove r dRec.Dependents

            for d in Set.difference newDeps oldDeps do
                let dRec = this.GetOrCreate d
                dRec.Dependents <- Set.add r dRec.Dependents

            rec'.Raw <- raw
            rec'.Content <- content
            rec'.Deps <- newDeps

            this.Recalculate r
            Ok()

    member this.Clear(r: CellRef) : unit = this.SetCell(r, "") |> ignore

    /// The smallest rectangle (0-based, inclusive) containing every cell
    /// that currently has non-empty content. None if the sheet is empty.
    member this.Extent : (CellRef * CellRef) option =
        let withContent =
            cells
            |> Seq.filter (fun kv -> kv.Value.Content <> Empty)
            |> Seq.map (fun kv -> kv.Key)
            |> List.ofSeq

        match withContent with
        | [] -> None
        | first :: rest ->
            let folder (loCol, loRow, hiCol, hiRow) (r: CellRef) =
                (min loCol r.Col, min loRow r.Row, max hiCol r.Col, max hiRow r.Row)

            let loCol, loRow, hiCol, hiRow =
                rest |> List.fold folder (first.Col, first.Row, first.Col, first.Row)

            Some({ Col = loCol; Row = loRow }, { Col = hiCol; Row = hiRow })
