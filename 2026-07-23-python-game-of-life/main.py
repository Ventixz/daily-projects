#!/usr/bin/env python3
"""Terminal animation of Conway's Game of Life.

    python3 main.py --pattern glider --generations 24
    python3 main.py --list
"""

import argparse
import os
import time

from gameoflife import Board, patterns


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pattern",
        default="glider",
        choices=sorted(patterns.ALL_PATTERNS),
        help="starting pattern (default: glider)",
    )
    parser.add_argument(
        "--generations", type=int, default=30,
        help="number of generations to simulate (default: 30)",
    )
    parser.add_argument(
        "--fps", type=float, default=6.0,
        help="frames per second (default: 6)",
    )
    parser.add_argument(
        "--list", action="store_true",
        help="list available patterns and exit",
    )
    return parser.parse_args()


def clear_screen():
    print("\033c", end="")


def animate(board, generations, fps):
    delay = 1.0 / fps if fps > 0 else 0
    for _ in range(generations + 1):
        clear_screen()
        print(f"generation {board.generation}  (population {board.population()})\n")
        print(board.render())
        if board.generation < generations:
            time.sleep(delay)
            board.step()
    print()


def main():
    args = parse_args()

    if args.list:
        for name in sorted(patterns.ALL_PATTERNS):
            print(name)
        return

    pattern = patterns.ALL_PATTERNS[args.pattern]
    board = Board.from_pattern(pattern)
    animate(board, args.generations, args.fps)


if __name__ == "__main__":
    main()
