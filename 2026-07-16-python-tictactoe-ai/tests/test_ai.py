import random

from tictactoe.ai import best_move
from tictactoe.board import PLAYER_O, PLAYER_X, Board, other_player


def test_takes_immediate_win():
    # X to move at position 2 completes the top row.
    board = Board([PLAYER_X, PLAYER_X, " ", " ", PLAYER_O, " ", PLAYER_O, " ", " "])
    assert best_move(board, PLAYER_X) == 2


def test_blocks_opponent_win():
    # O threatens the left column at position 6; X must block there.
    board = Board([PLAYER_O, " ", PLAYER_X, PLAYER_O, PLAYER_X, " ", " ", " ", " "])
    assert best_move(board, PLAYER_X) == 6


def test_prefers_faster_win_over_slower_one():
    # X can win immediately at 2 to complete the top row, rather than setting
    # up a slower win elsewhere.
    board = Board([PLAYER_X, PLAYER_X, " ", " ", PLAYER_O, " ", " ", " ", " "])
    assert best_move(board, PLAYER_X) == 2


def _play_ai_vs_random(ai_mark: str, rng: random.Random) -> str | None:
    """Play one game where `ai_mark` always uses the AI and the other side
    picks a uniformly random legal move. Returns the winner, or None for a draw."""
    board = Board()
    turn = PLAYER_X
    while not board.is_terminal:
        if turn == ai_mark:
            pos = best_move(board, turn)
        else:
            pos = rng.choice(board.available_moves())
        board.make_move(pos, turn)
        turn = other_player(turn)
    return board.winner


def test_ai_never_loses_to_a_random_opponent():
    rng = random.Random(0)
    for ai_mark in (PLAYER_X, PLAYER_O):
        for _ in range(200):
            winner = _play_ai_vs_random(ai_mark, rng)
            assert winner != other_player(ai_mark)


def test_ai_vs_ai_always_draws():
    board = Board()
    turn = PLAYER_X
    while not board.is_terminal:
        pos = best_move(board, turn)
        board.make_move(pos, turn)
        turn = other_player(turn)
    assert board.winner is None
    assert board.is_full
