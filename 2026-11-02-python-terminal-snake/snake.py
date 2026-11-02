"""Terminal Snake: a pure, seedable game core plus a curses front end."""
import random
from collections import deque

DIRS = {"up": (-1, 0), "down": (1, 0), "left": (0, -1), "right": (0, 1)}
OPPOSITE = {"up": "down", "down": "up", "left": "right", "right": "left"}


class Game:
    def __init__(self, rows=15, cols=30, seed=None):
        self.rows, self.cols = rows, cols
        self.rng = random.Random(seed)
        self.snake = deque([(rows // 2, cols // 2 - i) for i in range(3)])  # head first
        self.direction = "right"
        self.score = 0
        self.alive = True
        self.food = self._place_food()

    def _place_food(self):
        free = [(r, c) for r in range(self.rows) for c in range(self.cols)
                if (r, c) not in self.snake]
        return self.rng.choice(free) if free else None

    def turn(self, direction):
        """Ignore 180-degree reversals (they would be instant self-collision)."""
        if direction in DIRS and direction != OPPOSITE[self.direction]:
            self.direction = direction

    def step(self):
        if not self.alive:
            return
        dr, dc = DIRS[self.direction]
        hr, hc = self.snake[0]
        head = (hr + dr, hc + dc)
        grows = head == self.food
        # The tail cell is vacated this tick unless we grow, so moving into it is legal.
        body = list(self.snake) if grows else list(self.snake)[:-1]
        if not (0 <= head[0] < self.rows and 0 <= head[1] < self.cols) or head in body:
            self.alive = False
            return
        self.snake.appendleft(head)
        if grows:
            self.score += 1
            self.food = self._place_food()
            if self.food is None:  # board full: win
                self.alive = False
        else:
            self.snake.pop()

    def render(self):
        grid = [["." for _ in range(self.cols)] for _ in range(self.rows)]
        if self.food:
            grid[self.food[0]][self.food[1]] = "*"
        for i, (r, c) in enumerate(self.snake):
            grid[r][c] = "@" if i == 0 else "o"
        return "\n".join("".join(row) for row in grid)


def main(stdscr):
    import curses
    curses.curs_set(0)
    stdscr.nodelay(True)
    keys = {curses.KEY_UP: "up", curses.KEY_DOWN: "down",
            curses.KEY_LEFT: "left", curses.KEY_RIGHT: "right",
            ord("w"): "up", ord("s"): "down", ord("a"): "left", ord("d"): "right"}
    game = Game()
    while game.alive:
        k = stdscr.getch()
        if k == ord("q"):
            break
        if k in keys:
            game.turn(keys[k])
        game.step()
        stdscr.erase()
        stdscr.addstr(0, 0, f"Score: {game.score}  (arrows/WASD, q quits)")
        for i, line in enumerate(game.render().split("\n")):
            stdscr.addstr(i + 1, 0, line)
        stdscr.refresh()
        curses.napms(max(50, 150 - game.score * 5))
    return game.score


if __name__ == "__main__":
    import curses
    print("Final score:", curses.wrapper(main))
