#!/usr/bin/env python3
"""Converts a binary P6 PPM to PNG using only the standard library.

No Pillow/ImageMagick was available in the build environment, and this
project's whole point is staying free of installed dependencies -- so
this is a from-scratch (if minimal) PNG encoder: one IDAT chunk holding
zlib-compressed rows, each prefixed with filter type 0 (none).

(Carried over from 2026-08-08-cpp-raytracer/tools/ppm_to_png.py, same repo,
same author -- not re-derived.)
"""
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise ValueError("not a binary PPM (P6)")
    # Tokenize the three whitespace-separated header fields after "P6",
    # skipping '#' comment lines, then the single byte of whitespace
    # before the pixel data.
    pos = 2
    fields = []
    while len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1  # single whitespace byte before pixel data
    width, height, maxval = fields
    assert maxval == 255
    pixels = data[pos:pos + width * height * 3]
    return width, height, pixels


def write_png(path, width, height, pixels):
    def chunk(tag, payload):
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    raw = bytearray()
    stride = width * 3
    for row in range(height):
        raw.append(0)  # filter type: none
        raw.extend(pixels[row * stride:(row + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit RGB
    idat = zlib.compress(bytes(raw), 9)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    w, h, px = read_ppm(src)
    write_png(dst, w, h, px)
    print(f"wrote {dst} ({w}x{h})")
