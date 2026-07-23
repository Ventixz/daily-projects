"""Conway's Game of Life on an unbounded grid.

The board is a set of (row, col) coordinates of live cells rather than a
fixed 2D array. Nothing needs to be pre-sized, and a glider can fly off
into arbitrarily large coordinates without the board ever being resized.
"""

from collections import Counter

_NEIGHBOR_OFFSETS = [
    (dr, dc)
    for dr in (-1, 0, 1)
    for dc in (-1, 0, 1)
    if (dr, dc) != (0, 0)
]


def next_generation(live_cells):
    """Return the frozenset of live cells one tick after `live_cells`.

    Rather than scanning every cell of a bounded grid, this tallies how
    many live neighbors *each dead-or-alive cell adjacent to a live cell*
    has, by fanning each live cell out to its 8 neighbors and counting
    with a Counter. Any cell that never shows up in the tally has zero
    live neighbors and cannot be alive next generation, so it's skipped
    entirely — the work scales with the population, not the board size.
    """
    neighbor_counts = Counter()
    for row, col in live_cells:
        for dr, dc in _NEIGHBOR_OFFSETS:
            neighbor_counts[(row + dr, col + dc)] += 1

    return frozenset(
        cell
        for cell, count in neighbor_counts.items()
        if count == 3 or (count == 2 and cell in live_cells)
    )


class Board:
    """A Game of Life board: a live-cell set plus a generation counter."""

    def __init__(self, live_cells=()):
        self.live_cells = frozenset(live_cells)
        self.generation = 0

    @classmethod
    def from_pattern(cls, pattern, origin=(0, 0)):
        """Build a board from a pattern (an iterable of (row, col) offsets)."""
        origin_row, origin_col = origin
        cells = {(r + origin_row, c + origin_col) for r, c in pattern}
        return cls(cells)

    def step(self):
        """Advance the board by one generation in place, and return self."""
        self.live_cells = next_generation(self.live_cells)
        self.generation += 1
        return self

    def is_alive(self, cell):
        return cell in self.live_cells

    def population(self):
        return len(self.live_cells)

    def bounding_box(self):
        """Return (min_row, min_col, max_row, max_col) spanning all live cells.

        Returns None for an empty board — there is nothing to bound.
        """
        if not self.live_cells:
            return None
        rows = [r for r, _ in self.live_cells]
        cols = [c for _, c in self.live_cells]
        return min(rows), min(cols), max(rows), max(cols)

    def render(self, alive="#", dead=".", padding=1):
        """Render the live region as a text block, padded by `padding` cells."""
        box = self.bounding_box()
        if box is None:
            return "(empty)"
        min_row, min_col, max_row, max_col = box
        min_row -= padding
        min_col -= padding
        max_row += padding
        max_col += padding

        lines = []
        for row in range(min_row, max_row + 1):
            line = "".join(
                alive if (row, col) in self.live_cells else dead
                for col in range(min_col, max_col + 1)
            )
            lines.append(line)
        return "\n".join(lines)

    def __eq__(self, other):
        if not isinstance(other, Board):
            return NotImplemented
        return self.live_cells == other.live_cells

    def __repr__(self):
        return f"Board(generation={self.generation}, population={self.population()})"
