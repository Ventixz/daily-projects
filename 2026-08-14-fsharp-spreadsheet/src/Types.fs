namespace Spreadsheet

/// A cell reference, stored 0-based (row 0 = spreadsheet row "1", col 0 = column "A").
[<Struct; CustomComparison; CustomEquality>]
type CellRef =
    { Col: int
      Row: int }

    override this.Equals(o) =
        match o with
        | :? CellRef as other -> this.Col = other.Col && this.Row = other.Row
        | _ -> false

    override this.GetHashCode() = this.Row * 1_000_000 + this.Col

    interface System.IComparable with
        member this.CompareTo(o) =
            match o with
            | :? CellRef as other ->
                if this.Row <> other.Row then compare this.Row other.Row
                else compare this.Col other.Col
            | _ -> invalidArg "o" "not a CellRef"

type BinOp =
    | Add
    | Sub
    | Mul
    | Div
    | Pow

/// A parsed formula expression. Ranges (`A1:A3`) are only meaningful as
/// arguments to a function call -- they are not a standalone value.
type Expr =
    | ENum of float
    | EStr of string
    | ERef of CellRef
    | ERange of CellRef * CellRef
    | ENeg of Expr
    | EBinOp of BinOp * Expr * Expr
    | EFunc of string * Expr list

/// What a cell actually holds, before evaluation.
type CellContent =
    | Empty
    | ConstNum of float
    | ConstText of string
    | Formula of Expr

/// What a cell evaluates to.
type CellValue =
    | VNumber of float
    | VText of string
    | VEmpty
    | VError of string

module CellValue =
    let toDisplayString v =
        match v with
        | VNumber n ->
            // Trim trailing ".0" the way a spreadsheet cell would.
            if System.Double.IsNaN n then "#VALUE!"
            elif n = System.Math.Floor n && not (System.Double.IsInfinity n) && abs n < 1e15 then
                sprintf "%d" (int64 n)
            else
                sprintf "%g" n
        | VText s -> s
        | VEmpty -> ""
        | VError e -> e
