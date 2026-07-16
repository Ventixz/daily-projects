"""Minimax with alpha-beta pruning: an AI that can never lose at tic-tac-toe."""

from __future__ import annotations

from .board import Board, other_player


def _score(board: Board, root_mark: str, depth: int) -> int:
    winner = board.winner
    if winner == root_mark:
        return 10 - depth  # win faster is worth more
    if winner == other_player(root_mark):
        return depth - 10  # lose slower is worth more
    return 0


def minimax(
    board: Board,
    mark: str,
    root_mark: str,
    depth: int = 0,
    alpha: float = float("-inf"),
    beta: float = float("inf"),
) -> int:
    """Score `board` from root_mark's perspective, assuming both sides play optimally.

    `mark` is whoever moves next at this node; `root_mark` is fixed for the whole
    search so scores stay comparable across recursive calls.
    """
    if board.is_terminal:
        return _score(board, root_mark, depth)

    maximizing = mark == root_mark
    best = float("-inf") if maximizing else float("inf")

    for pos in board.available_moves():
        board.make_move(pos, mark)
        value = minimax(board, other_player(mark), root_mark, depth + 1, alpha, beta)
        board.undo_move(pos)

        if maximizing:
            best = max(best, value)
            alpha = max(alpha, best)
        else:
            best = min(best, value)
            beta = min(beta, best)

        if beta <= alpha:
            break  # the other side already has a better option elsewhere; stop exploring

    return best


def best_move(board: Board, mark: str) -> int:
    """Pick the move that maximizes `mark`'s minimax score."""
    moves = board.available_moves()
    if not moves:
        raise ValueError("no moves available")

    best_score = float("-inf")
    chosen = moves[0]
    for pos in moves:
        board.make_move(pos, mark)
        score = minimax(board, other_player(mark), root_mark=mark)
        board.undo_move(pos)
        if score > best_score:
            best_score = score
            chosen = pos
    return chosen
