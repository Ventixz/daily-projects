module Spreadsheet.Lexer

type Token =
    | TNum of float
    | TStr of string
    | TIdent of string
    | TPlus
    | TMinus
    | TStar
    | TSlash
    | TCaret
    | TLParen
    | TRParen
    | TColon
    | TComma
    | TEnd

exception LexError of string

/// Tokenizes the body of a formula (the part after the leading '=').
let tokenize (input: string) : Token list =
    let n = input.Length
    let tokens = ResizeArray<Token>()
    let mutable i = 0

    while i < n do
        let c = input.[i]

        if c = ' ' || c = '\t' then
            i <- i + 1
        elif c = '+' then
            tokens.Add TPlus
            i <- i + 1
        elif c = '-' then
            tokens.Add TMinus
            i <- i + 1
        elif c = '*' then
            tokens.Add TStar
            i <- i + 1
        elif c = '/' then
            tokens.Add TSlash
            i <- i + 1
        elif c = '^' then
            tokens.Add TCaret
            i <- i + 1
        elif c = '(' then
            tokens.Add TLParen
            i <- i + 1
        elif c = ')' then
            tokens.Add TRParen
            i <- i + 1
        elif c = ':' then
            tokens.Add TColon
            i <- i + 1
        elif c = ',' then
            tokens.Add TComma
            i <- i + 1
        elif c = '"' then
            let start = i + 1
            let mutable j = start
            while j < n && input.[j] <> '"' do
                j <- j + 1
            if j >= n then
                raise (LexError(sprintf "unterminated string literal starting at position %d" i))
            tokens.Add(TStr(input.Substring(start, j - start)))
            i <- j + 1
        elif System.Char.IsDigit c then
            let start = i
            let mutable j = i
            while j < n && System.Char.IsDigit input.[j] do
                j <- j + 1
            if j < n && input.[j] = '.' then
                j <- j + 1
                while j < n && System.Char.IsDigit input.[j] do
                    j <- j + 1
            let text = input.Substring(start, j - start)
            tokens.Add(TNum(System.Double.Parse(text, System.Globalization.CultureInfo.InvariantCulture)))
            i <- j
        elif System.Char.IsLetter c then
            let start = i
            let mutable j = i
            while j < n && System.Char.IsLetterOrDigit input.[j] do
                j <- j + 1
            tokens.Add(TIdent(input.Substring(start, j - start)))
            i <- j
        else
            raise (LexError(sprintf "unexpected character '%c' at position %d" c i))

    tokens.Add TEnd
    tokens |> List.ofSeq
