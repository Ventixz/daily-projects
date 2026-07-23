import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from gameoflife import Board, patterns


class TestEmptyBoard(unittest.TestCase):
    def test_empty_board_stays_empty(self):
        board = Board()
        board.step()
        self.assertEqual(board.population(), 0)
        self.assertEqual(board.live_cells, frozenset())


class TestStillLifes(unittest.TestCase):
    def test_block_is_unchanged_forever(self):
        board = Board.from_pattern(patterns.BLOCK)
        before = board.live_cells
        for _ in range(5):
            board.step()
            self.assertEqual(board.live_cells, before)


class TestOscillators(unittest.TestCase):
    def test_blinker_flips_between_two_states(self):
        board = Board.from_pattern(patterns.BLINKER, origin=(5, 5))
        gen0 = board.live_cells

        board.step()
        gen1 = board.live_cells
        self.assertNotEqual(gen0, gen1)
        self.assertEqual(len(gen1), 3)

        board.step()
        gen2 = board.live_cells
        self.assertEqual(gen0, gen2)

    def test_blinker_horizontal_becomes_vertical(self):
        # A horizontal 3-in-a-row centered at the origin should become
        # a vertical 3-in-a-row after one step.
        board = Board.from_pattern(patterns.BLINKER)
        board.step()
        expected = frozenset({(-1, 0), (0, 0), (1, 0)})
        self.assertEqual(board.live_cells, expected)

    def test_toad_has_period_two(self):
        board = Board.from_pattern(patterns.TOAD)
        gen0 = board.live_cells
        board.step()
        board.step()
        self.assertEqual(board.live_cells, gen0)

    def test_beacon_has_period_two(self):
        board = Board.from_pattern(patterns.BEACON)
        gen0 = board.live_cells
        board.step()
        board.step()
        self.assertEqual(board.live_cells, gen0)


class TestGlider(unittest.TestCase):
    def test_glider_translates_diagonally_after_four_generations(self):
        board = Board.from_pattern(patterns.GLIDER)
        start = board.live_cells

        for _ in range(4):
            board.step()

        shifted = frozenset((r + 1, c + 1) for r, c in start)
        self.assertEqual(board.live_cells, shifted)

    def test_glider_population_is_conserved(self):
        board = Board.from_pattern(patterns.GLIDER)
        self.assertEqual(board.population(), 5)
        for _ in range(8):
            board.step()
            self.assertEqual(board.population(), 5)


class TestUnderpopulationAndOverpopulation(unittest.TestCase):
    def test_lone_cell_dies(self):
        board = Board(live_cells={(0, 0)})
        board.step()
        self.assertEqual(board.population(), 0)

    def test_dead_cell_with_three_neighbors_is_born(self):
        # An L-tromino: (0,0), (0,1), (1,0) should birth (1,1).
        board = Board(live_cells={(0, 0), (0, 1), (1, 0)})
        board.step()
        self.assertIn((1, 1), board.live_cells)

    def test_overcrowded_cell_dies(self):
        # A live cell with 4 live neighbors dies of overpopulation.
        board = Board(live_cells={(0, 0), (-1, -1), (-1, 0), (-1, 1), (1, 0)})
        board.step()
        self.assertNotIn((0, 0), board.live_cells)


class TestBoardHelpers(unittest.TestCase):
    def test_generation_counter_increments(self):
        board = Board.from_pattern(patterns.GLIDER)
        self.assertEqual(board.generation, 0)
        board.step()
        board.step()
        self.assertEqual(board.generation, 2)

    def test_bounding_box_of_empty_board_is_none(self):
        self.assertIsNone(Board().bounding_box())

    def test_bounding_box_spans_all_live_cells(self):
        board = Board(live_cells={(0, 0), (3, 5), (-2, 1)})
        self.assertEqual(board.bounding_box(), (-2, 0, 3, 5))

    def test_render_matches_dimensions_of_padded_bounding_box(self):
        board = Board.from_pattern(patterns.BLOCK)
        rendered = board.render(padding=0)
        lines = rendered.split("\n")
        self.assertEqual(len(lines), 2)
        self.assertEqual(set(lines[0] + lines[1]), {"#"})

    def test_render_empty_board(self):
        self.assertEqual(Board().render(), "(empty)")


if __name__ == "__main__":
    unittest.main()
