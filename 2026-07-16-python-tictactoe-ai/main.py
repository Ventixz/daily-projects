#!/usr/bin/env python3
"""Play tic-tac-toe against an unbeatable minimax AI."""

from tictactoe.ai import best_move
from tictactoe.board import Board, PLAYER_O, PLAYER_X


def ask_mark() -> str:
    while True:
        choice = input("Play as X or O? (X moves first) [X/o]: ").strip().upper() or "X"
        if choice in (PLAYER_X, PLAYER_O):
            return choice
        print("Please enter X or O.")


def ask_move(board: Board) -> int:
    while True:
        raw = input(f"Your move {board.available_moves()}: ").strip()
        if raw.isdigit() and int(raw) in board.available_moves():
            return int(raw)
        print("Pick an open cell number from the list.")


def main() -> None:
    board = Board()
    human = ask_mark()
    ai = PLAYER_O if human == PLAYER_X else PLAYER_X
    turn = PLAYER_X

    print(board)
    while not board.is_terminal:
        if turn == human:
            pos = ask_move(board)
        else:
            pos = best_move(board, ai)
            print(f"AI plays {pos}")
        board.make_move(pos, turn)
        print()
        print(board)
        turn = PLAYER_O if turn == PLAYER_X else PLAYER_X

    print()
    if board.winner == human:
        print("You win?! Something's wrong with the AI.")
    elif board.winner == ai:
        print("AI wins.")
    else:
        print("Draw.")


if __name__ == "__main__":
    main()
