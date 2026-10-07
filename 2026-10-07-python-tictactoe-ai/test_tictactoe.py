import itertools
import unittest
from tictactoe import best_move, moves, other, winner


def all_human_lines(b, turn, ai):
    """Yield final results over every possible human strategy vs the AI."""
    w = winner(b)
    if w or not moves(b):
        yield w
        return
    if turn == ai:
        m = best_move(b, ai)
        b[m] = turn
        yield from all_human_lines(b, other(turn), ai)
        b[m] = " "
    else:
        for m in moves(b):
            b[m] = turn
            yield from all_human_lines(b, other(turn), ai)
            b[m] = " "


class T(unittest.TestCase):
    def test_ai_never_loses_as_either_side(self):
        for ai in "XO":
            res = set(all_human_lines([" "] * 9, "X", ai))
            self.assertNotIn(other(ai), res)

    def test_takes_win(self):
        b = list("XX OO    ")
        self.assertEqual(best_move(b, "X"), 2)

    def test_blocks(self):
        b = list("OO X     ")
        self.assertEqual(best_move(b, "X"), 2)

    def test_winner(self):
        self.assertEqual(winner(list("XXXOO    ")), "X")
        self.assertIsNone(winner([" "] * 9))


if __name__ == "__main__":
    unittest.main()
