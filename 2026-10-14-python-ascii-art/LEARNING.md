# Learning: ASCII Art Converter in Python

**Source:** the "ASCII Art" project in the Python section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Built with only the Python standard library (no Pillow), reading binary PGM/PPM images.

Run: `python3 asciiart.py sample.pgm 40 [--invert]`. Test: `python3 -m unittest`.

## Layout

- `asciiart.py` — `parse_pnm` (P5/P6 parser), `resize` (box-average), `to_ascii` (luminance -> character ramp), CLI.
- `test_asciiart.py` — 10 tests: headers/comments, luma, maxval scaling, bad input, resize, ramp ends, invert, aspect, monotonic gradient.
- `sample.pgm` — a generated radial gradient for the demo.

## What I learned

- **Image formats can be tiny.** PNM is a text header plus raw bytes; parsing it by hand (including `#` comments
  and the single whitespace byte before pixel data) demystifies what libraries do.
- **Luminance is a weighted sum**, not an average: Rec. 601 weights (0.299/0.587/0.114) match human perception.
- **Terminal cells are about twice as tall as wide**, so output height must be scaled by ~0.5 or the image looks stretched.
- **Downsampling should average**, not pick one pixel, otherwise detail aliases into noise.
- **Brightness-to-character mapping is just indexing** a ramp; inverting the ramp handles light-on-dark vs dark-on-light terminals.

## Known limits

- Only 8-bit binary PGM/PPM; no PNG/JPEG (would need Pillow or a decoder).
- No gamma correction and no dithering; ramp is a simple 10-step one.
- Resize is O(pixels) pure Python, so very large images are slow.
