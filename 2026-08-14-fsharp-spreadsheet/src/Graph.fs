module Spreadsheet.Graph

/// Every cell reachable by following "depends on me" edges out of `start`,
/// start included. This is the set that needs recomputing after `start`
/// changes -- everything else in the sheet is untouched by the edit.
let affectedSet (dependentsOf: CellRef -> Set<CellRef>) (start: CellRef) : Set<CellRef> =
    let mutable seen = Set.singleton start
    let stack = System.Collections.Generic.Stack<CellRef>()
    stack.Push start

    while stack.Count > 0 do
        let cur = stack.Pop()

        for next in dependentsOf cur do
            if not (Set.contains next seen) then
                seen <- Set.add next seen
                stack.Push next

    seen

/// Result of a topological sort restricted to a subset of cells: `Order` is
/// safe to evaluate cell-by-cell; `Cycle` is whatever never reached
/// in-degree zero, which is either on a cycle or reads (transitively,
/// within the subset) from one -- either way it can't be given a real
/// value.
type TopoResult =
    { Order: CellRef list
      Cycle: Set<CellRef> }

/// Kahn's algorithm restricted to `subset`: an edge dep -> cell exists only
/// when both ends are in `subset` (a dependency outside the subset already
/// has a settled value and doesn't need to be ordered relative to anything).
let topoOrder (depsOf: CellRef -> Set<CellRef>) (subset: Set<CellRef>) : TopoResult =
    let inDegree = System.Collections.Generic.Dictionary<CellRef, int>()
    let successors = System.Collections.Generic.Dictionary<CellRef, ResizeArray<CellRef>>()

    for cell in subset do
        inDegree.[cell] <- 0
        successors.[cell] <- ResizeArray()

    for cell in subset do
        let relevantDeps = depsOf cell |> Set.filter (fun d -> Set.contains d subset)
        inDegree.[cell] <- Set.count relevantDeps

        for dep in relevantDeps do
            successors.[dep].Add cell

    let queue = System.Collections.Generic.Queue<CellRef>()

    for cell in subset do
        if inDegree.[cell] = 0 then
            queue.Enqueue cell

    let order = ResizeArray<CellRef>()

    while queue.Count > 0 do
        let cur = queue.Dequeue()
        order.Add cur

        for next in successors.[cur] do
            inDegree.[next] <- inDegree.[next] - 1
            if inDegree.[next] = 0 then
                queue.Enqueue next

    let cycle = subset |> Set.filter (fun c -> inDegree.[c] > 0)
    { Order = List.ofSeq order; Cycle = cycle }
