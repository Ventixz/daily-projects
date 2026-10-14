"""Convert images to ASCII art using only the standard library.

Reads binary PGM (P5) and PPM (P6) files, converts pixels to luminance,
resamples to a character grid (correcting for tall character cells), and maps
brightness to a character ramp.
"""
import sys

RAMP = " .:-=+*#%@"  # dark -> light on a dark terminal (more ink = brighter)
CELL_ASPECT = 0.5  # a terminal cell is about twice as tall as it is wide


def _tokens(data, pos, count):
    """Read `count` whitespace-separated header tokens, skipping # comments."""
    out = []
    while len(out) < count:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        if start == pos:
            raise ValueError("truncated header")
        out.append(data[start:pos])
    return out, pos


def parse_pnm(data):
    """Return (width, height, rows) of luminance values scaled to 0..255."""
    magic = data[:2]
    if magic not in (b"P5", b"P6"):
        raise ValueError("only binary PGM (P5) and PPM (P6) are supported")
    (w, h, maxval), pos = _tokens(data, 2, 3)
    w, h, maxval = int(w), int(h), int(maxval)
    if maxval > 255:
        raise ValueError("16-bit images are not supported")
    pos += 1  # single whitespace byte after maxval
    channels = 1 if magic == b"P5" else 3
    body = data[pos:pos + w * h * channels]
    if len(body) < w * h * channels:
        raise ValueError("pixel data truncated")
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            i = (y * w + x) * channels
            if channels == 1:
                v = body[i]
            else:  # Rec. 601 luma
                v = 0.299 * body[i] + 0.587 * body[i + 1] + 0.114 * body[i + 2]
            row.append(v * 255 / maxval)
        rows.append(row)
    return w, h, rows


def resize(rows, new_w, new_h):
    """Box-average downsample (or nearest upsample) to new_w x new_h."""
    h, w = len(rows), len(rows[0])
    out = []
    for ny in range(new_h):
        y0, y1 = ny * h // new_h, max(ny * h // new_h + 1, (ny + 1) * h // new_h)
        row = []
        for nx in range(new_w):
            x0, x1 = nx * w // new_w, max(nx * w // new_w + 1, (nx + 1) * w // new_w)
            total = sum(rows[y][x] for y in range(y0, y1) for x in range(x0, x1))
            row.append(total / ((y1 - y0) * (x1 - x0)))
        out.append(row)
    return out


def to_ascii(rows, width=80, ramp=RAMP, invert=False):
    h, w = len(rows), len(rows[0])
    width = max(1, min(width, w)) if w >= 1 else 1
    height = max(1, round(h * width / w * CELL_ASPECT))
    small = resize(rows, width, height)
    if invert:
        ramp = ramp[::-1]
    n = len(ramp)
    lines = []
    for row in small:
        lines.append("".join(ramp[min(n - 1, int(v / 256 * n))] for v in row))
    return "\n".join(lines)


def main(argv):
    if len(argv) < 2:
        print("usage: asciiart.py IMAGE.pgm|ppm [width] [--invert]", file=sys.stderr)
        return 2
    args = [a for a in argv[2:] if not a.startswith("--")]
    width = int(args[0]) if args else 80
    with open(argv[1], "rb") as f:
        _, _, rows = parse_pnm(f.read())
    print(to_ascii(rows, width, invert="--invert" in argv))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
