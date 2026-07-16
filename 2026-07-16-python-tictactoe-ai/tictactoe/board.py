"""Tic-tac-toe board state, moves, and win detection."""

from __future__ import annotations

EMPTY = " "
PLAYER_X = "X"
PLAYER_O = "O"

WIN_LINES = (
    (0, 1, 2), (3, 4, 5), (6, 7, 8),  # rows
    (0, 3, 6), (1, 4, 7), (2, 5, 8),  # columns
    (0, 4, 8), (2, 4, 6),             # diagonals
)


def other_player(mark: str) -> str:
    return PLAYER_O if mark == PLAYER_X else PLAYER_X


class Board:
    """A 3x3 board stored as 9 cells, indexed left-to-right, top-to-bottom."""

    def __init__(self, cells: list[str] | None = None):
        self.cells = list(cells) if cells is not None else [EMPTY] * 9

    def available_moves(self) -> list[int]:
        return [i for i, mark in enumerate(self.cells) if mark == EMPTY]

    def make_move(self, pos: int, mark: str) -> None:
        if self.cells[pos] != EMPTY:
            raise ValueError(f"cell {pos} is already occupied")
        self.cells[pos] = mark

    def undo_move(self, pos: int) -> None:
        self.cells[pos] = EMPTY

    @property
    def winner(self) -> str | None:
        for a, b, c in WIN_LINES:
            if self.cells[a] != EMPTY and self.cells[a] == self.cells[b] == self.cells[c]:
                return self.cells[a]
        return None

    @property
    def is_full(self) -> bool:
        return EMPTY not in self.cells

    @property
    def is_terminal(self) -> bool:
        return self.winner is not None or self.is_full

    def __str__(self) -> str:
        rows = []
        for r in range(3):
            row = self.cells[r * 3:(r + 1) * 3]
            rows.append(" | ".join(
                mark if mark != EMPTY else str(r * 3 + i)
                for i, mark in enumerate(row)
            ))
        return "\n---------\n".join(rows)
