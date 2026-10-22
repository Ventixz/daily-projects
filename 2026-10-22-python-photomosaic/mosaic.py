"""Photomosaic generator using only the standard library.

Images are plain PPM (P6). The target image is cut into tiles; each tile is
replaced by the library image whose average colour is closest.
"""
import argparse
import random
import sys


class Image:
    def __init__(self, width, height, pixels):
        if len(pixels) != width * height * 3:
            raise ValueError("pixel buffer does not match dimensions")
        self.width, self.height, self.pixels = width, height, bytearray(pixels)

    def get(self, x, y):
        i = (y * self.width + x) * 3
        return self.pixels[i], self.pixels[i + 1], self.pixels[i + 2]


def parse_ppm(data):
    pos = 0
    tokens = []
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        if start == pos:
            raise ValueError("truncated PPM header")
        tokens.append(data[start:pos])
    if tokens[0] != b"P6":
        raise ValueError("only binary P6 PPM is supported")
    w, h, maxval = (int(t) for t in tokens[1:])
    if w <= 0 or h <= 0 or not 0 < maxval < 256:
        raise ValueError("unsupported PPM dimensions or maxval")
    body = data[pos + 1:pos + 1 + w * h * 3]
    if len(body) != w * h * 3:
        raise ValueError("truncated PPM pixel data")
    if maxval != 255:
        body = bytes(v * 255 // maxval for v in body)
    return Image(w, h, body)


def write_ppm(img):
    return b"P6\n%d %d\n255\n" % (img.width, img.height) + bytes(img.pixels)


def resize(img, w, h):
    """Box-average resize (nearest-neighbour when upscaling)."""
    out = bytearray(w * h * 3)
    for y in range(h):
        y0 = y * img.height // h
        y1 = max(y0 + 1, (y + 1) * img.height // h)
        for x in range(w):
            x0 = x * img.width // w
            x1 = max(x0 + 1, (x + 1) * img.width // w)
            sums = [0, 0, 0]
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    p = img.get(xx, yy)
                    sums[0] += p[0]; sums[1] += p[1]; sums[2] += p[2]
            n = (y1 - y0) * (x1 - x0)
            i = (y * w + x) * 3
            out[i:i + 3] = bytes(s // n for s in sums)
    return Image(w, h, out)


def average_colour(img, x0=0, y0=0, x1=None, y1=None):
    x1 = img.width if x1 is None else x1
    y1 = img.height if y1 is None else y1
    r = g = b = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = img.get(x, y)
            r += p[0]; g += p[1]; b += p[2]
    n = (x1 - x0) * (y1 - y0)
    return r / n, g / n, b / n


def distance(a, b):
    return sum((p - q) ** 2 for p, q in zip(a, b))


def best_match(colour, averages, used=None, max_reuse=None):
    """Index of the closest tile, skipping ones used max_reuse times already."""
    best, best_d = None, None
    for i, avg in enumerate(averages):
        if max_reuse is not None and used is not None and used.get(i, 0) >= max_reuse:
            continue
        d = distance(colour, avg)
        if best_d is None or d < best_d:
            best, best_d = i, d
    if best is None:
        raise ValueError("no tile available (library too small for max_reuse)")
    return best


def build_mosaic(target, library, tile, max_reuse=None):
    """Replace each tile x tile block of target with the closest library image."""
    if tile <= 0:
        raise ValueError("tile size must be positive")
    cols, rows = target.width // tile, target.height // tile
    if cols == 0 or rows == 0:
        raise ValueError("target smaller than one tile")
    thumbs = [resize(im, tile, tile) for im in library]
    averages = [average_colour(t) for t in thumbs]
    out = Image(cols * tile, rows * tile, bytes(cols * tile * rows * tile * 3))
    used = {}
    for row in range(rows):
        for col in range(cols):
            colour = average_colour(target, col * tile, row * tile,
                                    (col + 1) * tile, (row + 1) * tile)
            k = best_match(colour, averages, used, max_reuse)
            used[k] = used.get(k, 0) + 1
            for y in range(tile):
                dst = ((row * tile + y) * out.width + col * tile) * 3
                src = y * tile * 3
                out.pixels[dst:dst + tile * 3] = thumbs[k].pixels[src:src + tile * 3]
    return out


def solid(w, h, rgb):
    return Image(w, h, bytes(rgb) * (w * h))


def demo_library(n=64, size=8, seed=1):
    """Generated tiles: a colour cube sample with a light diagonal stripe."""
    rng = random.Random(seed)
    lib = []
    for _ in range(n):
        base = [rng.randrange(256) for _ in range(3)]
        img = solid(size, size, base)
        for d in range(size):
            i = (d * size + d) * 3
            img.pixels[i:i + 3] = bytes(min(255, c + 40) for c in base)
        lib.append(img)
    return lib


def demo_target(w=96, h=64):
    px = bytearray()
    for y in range(h):
        for x in range(w):
            px += bytes((x * 255 // (w - 1), y * 255 // (h - 1), 128))
    return Image(w, h, px)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Build a photomosaic from PPM files.")
    ap.add_argument("target", nargs="?", help="target P6 PPM (default: generated gradient)")
    ap.add_argument("library", nargs="*", help="tile P6 PPMs (default: generated tiles)")
    ap.add_argument("-t", "--tile", type=int, default=8)
    ap.add_argument("-r", "--max-reuse", type=int, default=None)
    ap.add_argument("-o", "--output", default="mosaic.ppm")
    args = ap.parse_args(argv)
    try:
        target = parse_ppm(open(args.target, "rb").read()) if args.target else demo_target()
        library = [parse_ppm(open(p, "rb").read()) for p in args.library] or demo_library()
        out = build_mosaic(target, library, args.tile, args.max_reuse)
    except (OSError, ValueError) as e:
        print("error:", e, file=sys.stderr)
        return 1
    with open(args.output, "wb") as f:
        f.write(write_ppm(out))
    print(f"wrote {args.output} ({out.width}x{out.height}, {len(library)} tiles)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
