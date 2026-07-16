# Tic-Tac-Toe AI (Python)

**Source:** [Python → Write a Tic-Tac-Toe AI](https://robertheaton.com/2018/10/09/programming-projects-for-advanced-beginners-3-a/),
listed under Python/Miscellaneous in
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting rather than scaffolded-then-attempted, so this folder
contains the finished implementation directly at the project root (no separate `reference/`).

## What it is

A terminal tic-tac-toe game against an AI that cannot lose. `tictactoe/board.py` holds board
state and win detection; `tictactoe/ai.py` implements minimax search with alpha-beta pruning to
pick moves; `main.py` is a small CLI that lets a human play either mark against it.

## Run it

```bash
cd 2026-07-16-python-tictactoe-ai
python3 main.py
```

You'll be asked to play X or O, then prompted for a cell number (0-8, shown on the board where
a cell is still open). The AI always responds instantly since alpha-beta pruning keeps the
search cheap even on the very first, widest-branching move.

Run the test suite with:

```bash
pip install --user pytest   # if not already installed
pytest -q
```

## What it actually teaches

- **Minimax as a search over the full game tree.** At each node, the side to move picks the
  child score that's best *for them* — maximizing for one mark, minimizing for the other — and
  that alternation is the entire algorithm. `tictactoe/ai.py`'s `minimax` recurses to a terminal
  board (a win, loss, or draw) and scores it, then those scores bubble back up through every
  intermediate choice.
- **Alpha-beta pruning as "stop looking once it can't matter."** `alpha` and `beta` track the
  best score the maximizing and minimizing sides can already guarantee elsewhere in the tree.
  The moment a branch's value can no longer beat that guarantee (`beta <= alpha`), the rest of
  that branch is skipped — the parent will never choose it regardless of what's left to find.
  This doesn't change the answer, only how much of the tree gets visited: an empty board has
  255,168 terminal leaf nodes and 549,946 nodes total (checked by walking the unpruned tree
  directly), and alpha-beta pruning is why the AI's opening move still comes back instantly.
- **Depth-biased scoring to make the AI play *well*, not just *safely*.** Scoring every win as
  `+10` and every loss as `-10` produces a technically-unbeatable AI that's also happy to draw
  out a loss it can't avoid, or dawdle on a win it could take immediately. Subtracting/adding
  `depth` (`10 - depth` for a win, `depth - 10` for a loss) makes faster wins score higher and
  slower losses score higher, so the AI prefers to close out winning lines immediately and to
  delay forced losses as long as possible — see `test_prefers_faster_win_over_slower_one`.
- **Mutate-and-undo beats rebuilding state on every recursive call.** `board.make_move` /
  `board.undo_move` mutate the same `Board` in place across the whole recursion instead of
  copying nine cells at every one of the 549,946 nodes minimax can visit. For a board this
  small it wouldn't matter, but it's the same shape used for search trees of any real size
  (chess engines do the identical make/unmake pattern), so it's worth building the habit here.
- **Testing an "unbeatable" claim by actually attacking it.** `test_ai_never_loses_to_a_random_opponent`
  runs 400 full games against a uniformly random opponent (seeded for determinism) with the AI
  on each side in turn, and `test_ai_vs_ai_always_draws` confirms two perfect players always draw.
  Asserting the property directly (never loses, across many games) catches a broken alpha-beta
  cutoff or a scoring-sign flip that a single hand-picked example would happily miss.

## What I'd add next (stretch goals I skipped for scope)

- A difficulty knob that caps search depth or occasionally picks a random legal move, so the AI
  is beatable for players who don't want a wall.
- Board sizes other than 3x3 (the win-line table and scoring are the only 3x3-specific parts —
  everything else in `ai.py` already treats the board as opaque).
- A transposition table to cache scores for boards reachable via different move orders — overkill
  for tic-tac-toe's tiny tree, but the natural next step once alpha-beta pruning isn't enough.
