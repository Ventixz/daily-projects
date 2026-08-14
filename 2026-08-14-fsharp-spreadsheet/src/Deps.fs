module Spreadsheet.Deps

/// All cells a range covers, inclusive, regardless of which corner is
/// top-left -- `SUM(B3:A1)` should mean the same thing as `SUM(A1:B3)`.
let expandRange (a: CellRef) (b: CellRef) : CellRef seq =
    let colLo, colHi = min a.Col b.Col, max a.Col b.Col
    let rowLo, rowHi = min a.Row b.Row, max a.Row b.Row

    seq {
        for row in rowLo..rowHi do
            for col in colLo..colHi do
                yield { Col = col; Row = row }
    }

/// The set of cells a formula reads from, used both to build the
/// dependency graph and to detect cycles before committing an edit.
let rec dependencies (e: Expr) : Set<CellRef> =
    match e with
    | ENum _
    | EStr _ -> Set.empty
    | ERef r -> Set.singleton r
    | ERange(a, b) -> expandRange a b |> Set.ofSeq
    | ENeg inner -> dependencies inner
    | EBinOp(_, l, r) -> Set.union (dependencies l) (dependencies r)
    | EFunc(_, args) -> args |> List.map dependencies |> List.fold Set.union Set.empty

let contentDependencies (c: CellContent) : Set<CellRef> =
    match c with
    | Formula e -> dependencies e
    | Empty
    | ConstNum _
    | ConstText _ -> Set.empty
