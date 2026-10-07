"""Tic-Tac-Toe with an unbeatable minimax AI (alpha-beta pruned). Stdlib only."""
import sys

LINES = [(0, 1, 2), (3, 4, 5), (6, 7, 8), (0, 3, 6), (1, 4, 7), (2, 5, 8),
         (0, 4, 8), (2, 4, 6)]


def winner(b):
    for a, c, d in LINES:
        if b[a] != " " and b[a] == b[c] == b[d]:
            return b[a]
    return None


def moves(b):
    return [i for i, v in enumerate(b) if v == " "]


def other(p):
    return "O" if p == "X" else "X"


def minimax(b, player, me, alpha=-10, beta=10, depth=0):
    """Score from `me`'s view: win = 10 - depth, loss = depth - 10, draw = 0."""
    w = winner(b)
    if w:
        return 10 - depth if w == me else depth - 10
    ms = moves(b)
    if not ms:
        return 0
    maximizing = player == me
    best = -10 if maximizing else 10
    for m in ms:
        b[m] = player
        s = minimax(b, other(player), me, alpha, beta, depth + 1)
        b[m] = " "
        if maximizing:
            best = max(best, s)
            alpha = max(alpha, best)
        else:
            best = min(best, s)
            beta = min(beta, best)
        if beta <= alpha:
            break
    return best


def best_move(b, player):
    best, pick = -99, None
    for m in moves(b):
        b[m] = player
        s = minimax(b, other(player), player, depth=1)
        b[m] = " "
        if s > best:
            best, pick = s, m
    return pick


def render(b):
    cell = [v if v != " " else str(i + 1) for i, v in enumerate(b)]
    rows = [" | ".join(cell[r * 3:r * 3 + 3]) for r in range(3)]
    return "\n--+---+--\n".join(rows)


def play(human="X", inp=input, out=print):
    b = [" "] * 9
    ai, turn = other(human), "X"
    while True:
        out(render(b))
        w = winner(b)
        if w or not moves(b):
            out(f"{w} wins!" if w else "Draw.")
            return w
        if turn == human:
            try:
                m = int(inp("Your move (1-9): ")) - 1
            except ValueError:
                out("Enter a number 1-9.")
                continue
            if m not in moves(b):
                out("Invalid move.")
                continue
        else:
            m = best_move(b, ai)
            out(f"AI plays {m + 1}")
        b[m] = turn
        turn = other(turn)


if __name__ == "__main__":
    play("O" if len(sys.argv) > 1 and sys.argv[1] == "O" else "X")
