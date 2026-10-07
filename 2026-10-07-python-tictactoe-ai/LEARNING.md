# Learning: A Tic-Tac-Toe AI in Python

**Source:** ["Write a Tic-Tac-Toe AI"](https://robertheaton.com/2018/10/09/programming-projects-for-advanced-beginners-3-a/)
from the Python section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

One file (`tictactoe.py`), Python 3 standard library only. Play with
`python3 tictactoe.py` (or `python3 tictactoe.py O` to move second); test with
`python3 -m unittest`.

## What it is

- A 9-cell board as a flat list, with the 8 winning lines as index triples.
- A **minimax** search with **alpha-beta pruning** that picks the AI's move.
- A text UI, with `play()` taking injectable `inp`/`out` so it can be tested.

## What I learned

- **Minimax is just "assume the opponent plays best".** The AI maximizes its score on its
  turns and minimizes on the opponent's. Tic-tac-toe has few enough states to search the
  whole tree.
- **Score by depth, not just win/loss.** Using `10 - depth` for a win makes the AI take
  the fastest win and delay a loss, instead of dawdling when it is already winning.
- **Alpha-beta prunes branches that cannot change the answer.** Once `beta <= alpha` the
  rest of a node's children are irrelevant. Same result, far fewer nodes.
- **Make/undo beats copying.** Mutating the board and restoring it after the recursive
  call avoids allocating a board per node.
- **Exhaustive testing is feasible.** The key test enumerates every possible human
  strategy against the AI, as both X and O, and asserts the human never wins. That proves
  "unbeatable" rather than sampling it.

## Known limits

- Fixed 3x3 board; the search would be far too slow unpruned on larger boards.
- Ties between equally good moves resolve to the lowest-numbered cell, so the AI is
  deterministic and a bit predictable.
- No difficulty levels or move randomisation.
