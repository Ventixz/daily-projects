module Spreadsheet.Tests.GraphTests

open Spreadsheet
open Spreadsheet.Graph
open Spreadsheet.Tests.TestHelper

let private cell col row : CellRef = { Col = col; Row = row }
let private a, b, c, d = cell 0 0, cell 0 1, cell 0 2, cell 0 3

let private indexOf (order: CellRef list) (r: CellRef) = order |> List.findIndex ((=) r)

let run () =
    // Linear chain: a -> b -> c (b depends on a, c depends on b).
    let deps =
        function
        | x when x = b -> Set.singleton a
        | x when x = c -> Set.singleton b
        | _ -> Set.empty

    let result = topoOrder deps (Set.ofList [ a; b; c ])
    assertEqual Set.empty result.Cycle "linear chain has no cycle"
    assertTrue (indexOf result.Order a < indexOf result.Order b) "a before b"
    assertTrue (indexOf result.Order b < indexOf result.Order c) "b before c"

    // Diamond: d depends on nothing; b and c both depend on d; a depends
    // on both b and c. d must come before both b and c, and both must come
    // before a -- but d must appear exactly once.
    let diamondDeps =
        function
        | x when x = a -> Set.ofList [ b; c ]
        | x when x = b -> Set.singleton d
        | x when x = c -> Set.singleton d
        | _ -> Set.empty

    let diamond = topoOrder diamondDeps (Set.ofList [ a; b; c; d ])
    assertEqual Set.empty diamond.Cycle "diamond has no cycle"
    assertEqual 4 diamond.Order.Length "diamond visits each cell exactly once, even though two paths lead to it"
    assertTrue (indexOf diamond.Order d < indexOf diamond.Order b) "d before b"
    assertTrue (indexOf diamond.Order d < indexOf diamond.Order c) "d before c"
    assertTrue (indexOf diamond.Order b < indexOf diamond.Order a) "b before a"
    assertTrue (indexOf diamond.Order c < indexOf diamond.Order a) "c before a"

    // Direct cycle: a -> b -> a.
    let directCycleDeps =
        function
        | x when x = a -> Set.singleton b
        | x when x = b -> Set.singleton a
        | _ -> Set.empty

    let directCycle = topoOrder directCycleDeps (Set.ofList [ a; b ])
    assertEqual (Set.ofList [ a; b ]) directCycle.Cycle "a directly-cyclic pair is entirely unorderable"
    assertEqual 0 directCycle.Order.Length "nothing in a 2-cycle can be safely ordered"

    // A cell downstream of a cycle, but not itself on it, is still
    // unorderable -- its value depends on cells that never settle.
    let downstreamOfCycleDeps =
        function
        | x when x = a -> Set.singleton b
        | x when x = b -> Set.singleton a
        | x when x = c -> Set.singleton b
        | _ -> Set.empty

    let downstream = topoOrder downstreamOfCycleDeps (Set.ofList [ a; b; c ])
    assertEqual (Set.ofList [ a; b; c ]) downstream.Cycle "a cell reading from a cycle is unorderable too"

    // affectedSet: dependents edges point the opposite direction from
    // deps edges. b, c and d all (transitively) depend on a.
    let dependentsOf =
        function
        | x when x = a -> Set.singleton b
        | x when x = b -> Set.singleton c
        | x when x = c -> Set.singleton d
        | _ -> Set.empty

    assertEqual (Set.ofList [ a; b; c; d ]) (affectedSet dependentsOf a) "affectedSet follows the whole dependent chain"
    assertEqual (Set.ofList [ c; d ]) (affectedSet dependentsOf c) "affectedSet from a mid-chain cell only reaches downstream"
    assertEqual (Set.singleton d) (affectedSet dependentsOf d) "a leaf with no dependents affects only itself"
