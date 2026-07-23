"""A handful of well-known Game of Life starting patterns.

Each pattern is a tuple of (row, col) offsets from an arbitrary local
origin, so any of them can be dropped onto a board at any position via
`Board.from_pattern(pattern, origin=(row, col))`.
"""

# Still lifes: unchanged generation after generation.
BLOCK = (
    (0, 0), (0, 1),
    (1, 0), (1, 1),
)

# Oscillators: cycle through a fixed set of states.
BLINKER = (
    (0, -1), (0, 0), (0, 1),
)  # period 2

TOAD = (
    (0, 1), (0, 2), (0, 3),
    (1, 0), (1, 1), (1, 2),
)  # period 2

BEACON = (
    (0, 0), (0, 1),
    (1, 0), (1, 1),
    (2, 2), (2, 3),
    (3, 2), (3, 3),
)  # period 2

# Spaceships: translate across the board as they oscillate.
GLIDER = (
    (0, 1),
    (1, 2),
    (2, 0), (2, 1), (2, 2),
)  # moves one cell diagonally every 4 generations

LWSS = (  # lightweight spaceship
    (0, 1), (0, 4),
    (1, 0),
    (2, 0), (2, 4),
    (3, 0), (3, 1), (3, 2), (3, 3),
)

ALL_PATTERNS = {
    "block": BLOCK,
    "blinker": BLINKER,
    "toad": TOAD,
    "beacon": BEACON,
    "glider": GLIDER,
    "lwss": LWSS,
}
