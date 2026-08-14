module Spreadsheet.Eval

/// Evaluates one cell's formula given a `lookup` for what other cells are
/// currently worth. Crucially, `lookup` returns an already-computed
/// CellValue -- it never walks into another cell's Expr and re-evaluates
/// it. That's what keeps a diamond-shaped dependency graph (D referenced by
/// both B and C, which are both referenced by A) linear instead of
/// exponential: by the time A is evaluated, B and C have each been
/// evaluated exactly once (by the topological order in Graph.fs) and their
/// results are just sitting there to be read. An evaluator that instead
/// called back into ERef's formula recursively would re-walk D once per
/// path to it -- fine for a diamond, ruinous for a deep lattice of them,
/// and an infinite loop the moment a cycle sneaks past dependency checking.

let private toNumber (v: CellValue) : Result<float, CellValue> =
    match v with
    | VNumber n -> Ok n
    | VEmpty -> Ok 0.0
    | VText _ -> Error(VError "#VALUE!")
    | VError _ -> Error v

let rec evalExpr (lookup: CellRef -> CellValue) (e: Expr) : CellValue =
    match e with
    | ENum n -> VNumber n
    | EStr s -> VText s
    | ERef r -> lookup r
    | ERange _ ->
        // A bare range only means something as a function argument;
        // `=A1:A3` on its own has no scalar value.
        VError "#VALUE!"
    | ENeg inner ->
        match toNumber (evalExpr lookup inner) with
        | Ok n -> VNumber(-n)
        | Error e -> e
    | EBinOp(op, l, r) ->
        let ln = toNumber (evalExpr lookup l)
        let rn = toNumber (evalExpr lookup r)

        match ln, rn with
        | Error e, _ -> e
        | _, Error e -> e
        | Ok a, Ok b ->
            match op with
            | Add -> VNumber(a + b)
            | Sub -> VNumber(a - b)
            | Mul -> VNumber(a * b)
            | Div -> if b = 0.0 then VError "#DIV/0!" else VNumber(a / b)
            | Pow -> VNumber(a ** b)
    | EFunc(name, args) -> evalFunc lookup name args

/// Numbers gathered from one function argument. A range tolerates blanks
/// and text in its cells (skipped, same as a spreadsheet SUM would do) but
/// still short-circuits on the first error cell it finds. A bare scalar
/// argument is stricter: text or blank there is a #VALUE! error, same as
/// using it directly in arithmetic.
and private argNumbers (lookup: CellRef -> CellValue) (arg: Expr) : Result<float list, CellValue> =
    match arg with
    | ERange(a, b) ->
        let mutable err = None
        let nums = ResizeArray()

        for r in Deps.expandRange a b do
            if err.IsNone then
                match lookup r with
                | VNumber n -> nums.Add n
                | VEmpty
                | VText _ -> ()
                | VError e -> err <- Some(VError e)

        match err with
        | Some e -> Error e
        | None -> Ok(List.ofSeq nums)
    | _ ->
        match toNumber (evalExpr lookup arg) with
        | Ok n -> Ok [ n ]
        | Error e -> Error e

and private evalFunc (lookup: CellRef -> CellValue) (name: string) (args: Expr list) : CellValue =
    let collected =
        args
        |> List.fold
            (fun acc arg ->
                match acc with
                | Error _ -> acc
                | Ok soFar ->
                    match argNumbers lookup arg with
                    | Ok ns -> Ok(soFar @ ns)
                    | Error e -> Error e)
            (Ok [])

    match collected with
    | Error e -> e
    | Ok nums ->
        match name with
        | "SUM" -> VNumber(List.sum nums)
        | "COUNT" -> VNumber(float nums.Length)
        | "AVERAGE" ->
            match nums with
            | [] -> VError "#DIV/0!"
            | _ -> VNumber(List.sum nums / float nums.Length)
        | "MIN" ->
            match nums with
            | [] -> VError "#VALUE!"
            | _ -> VNumber(List.min nums)
        | "MAX" ->
            match nums with
            | [] -> VError "#VALUE!"
            | _ -> VNumber(List.max nums)
        | _ -> VError "#NAME?"

/// Evaluates a whole cell's content (not just a Formula's Expr).
let evalContent (lookup: CellRef -> CellValue) (c: CellContent) : CellValue =
    match c with
    | Empty -> VEmpty
    | ConstNum n -> VNumber n
    | ConstText s -> VText s
    | Formula e -> evalExpr lookup e
