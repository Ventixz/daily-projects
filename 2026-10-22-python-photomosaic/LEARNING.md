# Learning: Photomosaic Art in Python

**Source:** the "Create photomosaic art" project (Robert Heaton, "Programming Project 4: Photomosaics") in the
Python → Miscellaneous section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Built with only the Python standard library, reading and writing binary PPM (P6) images.

Run: `python3 mosaic.py [target.ppm tile1.ppm tile2.ppm ...] [-t 8] [-r MAX_REUSE] [-o mosaic.ppm]`
(with no arguments it mosaics a generated gradient from generated tiles). Test: `python3 -m unittest`.

## Layout

- `mosaic.py` — `parse_ppm`/`write_ppm`, `resize` (box average), `average_colour`, `best_match`, `build_mosaic`, CLI.
- `test_mosaic.py` — 11 tests: PPM round trip/comments/maxval/errors, resize, regional average, matching,
  reuse limit, cropping to whole tiles, CLI success and failure.

## What I learned

- **A mosaic is nearest-neighbour search in colour space.** Reduce each target block and each library image to
  one average RGB value, then pick the library image with the smallest squared distance.
- **Precompute once.** Shrinking every library image and averaging it happens once up front; only the
  per-block lookup runs inside the loop.
- **Squared distance is enough** for "closest": skipping the square root doesn't change the ordering.
- **Repeats look bad.** A plain best match reuses the same few tiles; capping uses per tile (`--max-reuse`)
  spreads choices at the cost of slightly worse colour matches, and must fail clearly when the library is too
  small to satisfy the cap.
- **Edges need a policy.** Dimensions that aren't a multiple of the tile size are cropped to whole tiles.
- **Writing the image format by hand** (header, `#` comments, single whitespace byte before pixel data, maxval
  rescaling) removes the dependency on Pillow and shows how little a simple format needs.

## Known limits

- PPM P6 only, so photos need converting first; pure-Python loops make large images slow.
- Averaging in plain RGB isn't perceptually uniform; a Lab-space distance would match better.
- Matching is greedy in scan order, so the reuse cap favours tiles at the top of the picture.
