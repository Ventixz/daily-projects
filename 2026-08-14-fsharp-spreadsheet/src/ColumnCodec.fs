module Spreadsheet.ColumnCodec

open System.Text

/// Spreadsheet columns are "bijective base 26": A=1, B=2, ..., Z=26, AA=27.
/// There is no digit for zero, which is what makes this different from an
/// ordinary base-26 number -- see the note in colToLetters below.

/// 0-based column index -> letters ("A", "Z", "AA", "AB", ...).
let colToLetters (col: int) : string =
    if col < 0 then
        invalidArg "col" "column index must be >= 0"
    // Work in 1-based bijective base-26: repeatedly take ((n-1) mod 26) as
    // the next digit, then move to (n-1) / 26. The "-1" before each step is
    // what makes this bijective rather than plain base-26 -- without it,
    // column 26 ("Z") and column 27*26 ("AZ"-ish) collide, because plain
    // base-26 has no way to represent a leading "A" as anything other than
    // a leading zero, which doesn't exist in this alphabet.
    let sb = StringBuilder()
    let mutable n = col + 1
    while n > 0 do
        let rem = (n - 1) % 26
        sb.Insert(0, char (int 'A' + rem)) |> ignore
        n <- (n - 1) / 26
    sb.ToString()

/// letters ("A", "Z", "AA", ...) -> 0-based column index.
let lettersToCol (letters: string) : int option =
    if letters = "" || not (letters |> Seq.forall System.Char.IsAsciiLetterUpper) then
        None
    else
        let mutable n = 0
        for c in letters do
            n <- n * 26 + (int c - int 'A' + 1)
        Some(n - 1)

let cellRefToString (r: CellRef) : string =
    sprintf "%s%d" (colToLetters r.Col) (r.Row + 1)

/// Parses "A1", "AA12", etc. Case-insensitive on the letters.
let tryParseCellRef (s: string) : CellRef option =
    let s = s.Trim()
    let letters = s |> Seq.takeWhile System.Char.IsLetter |> Seq.toArray |> System.String
    let digits = s.Substring(letters.Length)

    if letters = "" || digits = "" || not (digits |> Seq.forall System.Char.IsDigit) then
        None
    else
        match lettersToCol (letters.ToUpperInvariant()), System.Int32.TryParse digits with
        | Some col, (true, row1based) when row1based >= 1 -> Some { Col = col; Row = row1based - 1 }
        | _ -> None
