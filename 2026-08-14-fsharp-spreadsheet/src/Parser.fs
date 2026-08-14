module Spreadsheet.Parser

open Spreadsheet.Lexer

exception ParseError of string

/// Recursive-descent parser over the token array, precedence low to high:
///   expr  := term (('+' | '-') term)*
///   term  := unary (('*' | '/') unary)*
///   unary := '-' unary | power
///   power := atom ('^' unary)?          -- right-associative
///   atom  := number | string | '(' expr ')' | ident [ '(' args ')' | (':' ident)? ]
///
/// A bare `A1:A3` is parsed at the atom level (wherever a cell reference can
/// appear), not just inside a function call's argument list -- it's Eval's
/// job, not the parser's, to reject a range used somewhere a scalar is
/// required. Keeping "is this syntactically valid" and "is this semantically
/// meaningful here" separate is what keeps this grammar small.
type private ParserState =
    { Tokens: Token[]
      mutable Pos: int }

let private peek (st: ParserState) = st.Tokens.[st.Pos]

let private advance (st: ParserState) =
    let t = st.Tokens.[st.Pos]
    if t <> TEnd then st.Pos <- st.Pos + 1
    t

let private expect (st: ParserState) (t: Token) =
    let got = advance st
    if got <> t then
        raise (ParseError(sprintf "expected %A but found %A" t got))

let rec private parseExpr (st: ParserState) : Expr =
    let mutable left = parseTerm st

    let mutable go = true
    while go do
        match peek st with
        | TPlus ->
            advance st |> ignore
            left <- EBinOp(Add, left, parseTerm st)
        | TMinus ->
            advance st |> ignore
            left <- EBinOp(Sub, left, parseTerm st)
        | _ -> go <- false

    left

and private parseTerm (st: ParserState) : Expr =
    let mutable left = parseUnary st

    let mutable go = true
    while go do
        match peek st with
        | TStar ->
            advance st |> ignore
            left <- EBinOp(Mul, left, parseUnary st)
        | TSlash ->
            advance st |> ignore
            left <- EBinOp(Div, left, parseUnary st)
        | _ -> go <- false

    left

and private parseUnary (st: ParserState) : Expr =
    match peek st with
    | TMinus ->
        advance st |> ignore
        ENeg(parseUnary st)
    | _ -> parsePower st

and private parsePower (st: ParserState) : Expr =
    let baseExpr = parseAtom st

    match peek st with
    | TCaret ->
        advance st |> ignore
        // Right-associative, and binds tighter than unary minus on its own
        // exponent (2^-1 means 2^(-1), same as most calculators).
        EBinOp(Pow, baseExpr, parseUnary st)
    | _ -> baseExpr

and private parseAtom (st: ParserState) : Expr =
    match peek st with
    | TNum n ->
        advance st |> ignore
        ENum n
    | TStr s ->
        advance st |> ignore
        EStr s
    | TLParen ->
        advance st |> ignore
        let e = parseExpr st
        expect st TRParen
        e
    | TIdent id ->
        advance st |> ignore

        if peek st = TLParen then
            advance st |> ignore
            let args = parseArgs st
            expect st TRParen
            EFunc(id.ToUpperInvariant(), args)
        else
            match ColumnCodec.tryParseCellRef id with
            | None -> raise (ParseError(sprintf "'%s' is not a valid cell reference or function name" id))
            | Some r1 ->
                match peek st with
                | TColon ->
                    advance st |> ignore

                    match peek st with
                    | TIdent id2 ->
                        advance st |> ignore

                        match ColumnCodec.tryParseCellRef id2 with
                        | Some r2 -> ERange(r1, r2)
                        | None -> raise (ParseError(sprintf "'%s' is not a valid cell reference" id2))
                    | other -> raise (ParseError(sprintf "expected a cell reference after ':' but found %A" other))
                | _ -> ERef r1
    | other -> raise (ParseError(sprintf "unexpected token %A" other))

and private parseArgs (st: ParserState) : Expr list =
    if peek st = TRParen then
        []
    else
        let first = parseExpr st
        let rest = ResizeArray [ first ]

        while peek st = TComma do
            advance st |> ignore
            rest.Add(parseExpr st)

        rest |> List.ofSeq

/// Parses a formula body (everything after the leading '='). Raises
/// ParseError on malformed input.
let parseFormula (body: string) : Expr =
    let st = { Tokens = tokenize body |> Array.ofList; Pos = 0 }
    let e = parseExpr st

    if peek st <> TEnd then
        raise (ParseError(sprintf "unexpected trailing input starting at %A" (peek st)))

    e

/// Parses raw cell text (as typed into a cell) into CellContent: a leading
/// '=' means formula, otherwise it's a number if it parses as one, and
/// plain text otherwise.
let parseContent (raw: string) : CellContent =
    if raw = "" then
        Empty
    elif raw.StartsWith("=") then
        Formula(parseFormula (raw.Substring(1)))
    else
        match System.Double.TryParse(raw, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture) with
        | true, n -> ConstNum n
        | false, _ -> ConstText raw
