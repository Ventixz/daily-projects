import unittest
from snake import Game


class T(unittest.TestCase):
    def g(self):
        return Game(rows=10, cols=10, seed=1)

    def test_initial(self):
        g = self.g()
        self.assertEqual(len(g.snake), 3)
        self.assertNotIn(g.food, g.snake)

    def test_moves_right(self):
        g = self.g(); head = g.snake[0]
        g.step()
        self.assertEqual(g.snake[0], (head[0], head[1] + 1))
        self.assertEqual(len(g.snake), 3)

    def test_reverse_ignored(self):
        g = self.g(); g.turn("left")
        self.assertEqual(g.direction, "right")

    def test_turn(self):
        g = self.g(); g.turn("up"); g.step()
        self.assertEqual(g.snake[0][0], 4)

    def test_wall_death(self):
        g = self.g()
        for _ in range(20): g.step()
        self.assertFalse(g.alive)

    def test_eat_grows_and_scores(self):
        g = self.g(); h = g.snake[0]
        g.food = (h[0], h[1] + 1); g.step()
        self.assertEqual(g.score, 1); self.assertEqual(len(g.snake), 4)
        self.assertNotIn(g.food, g.snake)

    def test_self_collision(self):
        g = self.g(); g.snake.clear()
        g.snake.extend([(5,5),(5,6),(6,6),(6,5),(6,4),(5,4),(4,4)])
        g.direction = "up"; g.food = (0, 0)
        g.turn("left"); g.step()  # head (5,5)->(5,4) is body
        self.assertFalse(g.alive)

    def test_tail_cell_is_legal(self):
        g = self.g(); g.snake.clear()
        g.snake.extend([(5,5),(5,6),(6,6),(6,5)]); g.direction = "left"
        g.food = (0, 0); g.turn("down"); g.step()  # into (6,5)=tail, vacated
        self.assertTrue(g.alive)

    def test_dead_game_frozen(self):
        g = self.g(); g.alive = False; h = g.snake[0]; g.step()
        self.assertEqual(g.snake[0], h)

    def test_seed_deterministic(self):
        self.assertEqual(Game(seed=7).food, Game(seed=7).food)

    def test_render(self):
        out = self.g().render().split("\n")
        self.assertEqual(len(out), 10)
        self.assertEqual(sum(l.count("@") for l in out), 1)
        self.assertEqual(sum(l.count("*") for l in out), 1)

    def test_win_on_full_board(self):
        g = Game(rows=1, cols=4, seed=0)  # snake of 3 + 1 food cell
        g.food = (0, 3); g.step()
        self.assertEqual(g.score, 1); self.assertFalse(g.alive); self.assertIsNone(g.food)


if __name__ == "__main__":
    unittest.main()
