# Let's Build a Browser Engine! (Rust)

**Source:** ["Let's build a browser engine!"](https://limpet.net/mbrubeck/2014/08/08/toy-layout-engine-1.html)
by Matt Brubeck (the "Robinson" series, parts 1-7: HTML, CSS, style, block
layout, and painting), from the Rust section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Zero external crates -- standard library only. The tutorial itself doesn't
use any, so this isn't a deviation the way skipping `hyper` or `bracket-lib`
was on other days; it's just following the brief.

## What it is

A pipeline that turns an HTML file and a CSS file into a rasterized image,
the same five stages a real browser's rendering path goes through before a
single pixel hits the screen:

```
HTML text --parse--> DOM tree --match CSS--> style tree --layout--> box tree --paint--> canvas --PPM/PNG
```

- `src/dom.rs` -- `Node` (`Text` or `Element`), `ElementData` with an
  attribute map, plus `id()`/`classes()` helpers used by selector matching.
- `src/html_parser.rs` -- recursive-descent parser: nested tags, quoted or
  bare attributes, void elements (`<br>`, `<img>`, ...) that never expect a
  closing tag, self-closing `/>` syntax, HTML comments, and permissive
  recovery when a closing tag doesn't match what's open (it's left for an
  ancestor to consume instead of panicking).
- `src/css.rs` -- parses a stylesheet into `Rule`s, each a list of
  `SimpleSelector`s (tag name + optional `#id` + any number of `.class`,
  no combinators) and `Declaration`s (keyword, px length, or `#rgb`
  /`#rrggbb`/`#rrggbbaa` color values). `Selector::specificity()` returns
  `(has_id, class_count, has_tag)`, the same tuple ordering the real CSS
  spec uses minus the inline-style and `!important` tiers this project has
  no use for.
- `src/style.rs` -- builds the *style tree*: DOM nodes paired with their
  matched, cascaded declarations. Matching rules are sorted by specificity
  ascending and applied in that order, so a lower-specificity match set
  early and a higher one overwrites it -- equal specificity falls back to
  source order for free, because that's the order `matching_rules` already
  visits the stylesheet in.
- `src/layout.rs` -- the block-layout algorithm: `Dimensions` (a content
  `Rect` plus `padding`/`border`/`margin` `EdgeSizes`), `LayoutBox` (a
  `BlockNode`, `InlineNode`, or `AnonymousBlock` -- the last one only
  exists so a block box with a mix of block and inline children has
  somewhere to put the inline runs). `calculate_block_width` solves the
  CSS2.1 width equation (`margin-left + border-left + padding-left + width
  + padding-right + border-right + margin-right = containing block width`)
  for whichever of `width`/`margin-left`/`margin-right` are `auto`;
  `calculate_block_height` lets an explicit `height` override the height
  computed by summing children's margin boxes. Inline boxes are built but
  never actually flowed (no text metrics, no line breaking) -- same scope
  limit the tutorial itself has.
- `src/painting.rs` -- walks the layout tree into a flat `DisplayList` of
  `SolidColor(color, rect)` commands (background rect, then the four
  border rects, per box, in tree order so overlaps paint correctly), then
  rasterizes that list onto a `Canvas` and serializes it as a binary P6
  PPM.
- `tools/ppm_to_png.py` -- carried over unchanged from
  `2026-08-08-cpp-raytracer/tools/`, same repo, same author.
- `pages/demo.html` + `pages/demo.css` -- a small nested layout (header,
  sidebar + main inside a content area, a bordered card, a footer) used to
  sanity-check the whole pipeline end to end; `renders/demo.ppm/.png` is
  its rendered output.
- 34 tests across five files: 8 HTML-parser tests (nesting, attributes,
  void elements, self-closing tags, comments, the multi-root-wraps-in-html
  rule), 7 CSS-parser tests (selector specificity ordering, px defaulting,
  3/6/8-digit hex colors), 8 style-tree tests (tag/class/id matching,
  specificity cascade, source-order tiebreaks, `display: none`), 6 layout
  tests (auto-width fill, `margin: auto` centering, padding/border not
  perturbing content width, vertical stacking arithmetic, overconstrained
  widths), 5 painting tests (canvas fill, background/border pixel
  boundaries, exact PPM byte length).

## Run it

```bash
cd 2026-08-26-rust-browser-engine
cargo test                                                    # 34 tests
cargo run --release -- pages/demo.html pages/demo.css renders/demo.ppm
python3 tools/ppm_to_png.py renders/demo.ppm renders/demo.png
```

Actual output:

```
$ cargo test
test result: ok. 8 passed; 0 failed  (test_html_parser)
test result: ok. 7 passed; 0 failed  (test_css)
test result: ok. 8 passed; 0 failed  (test_style)
test result: ok. 6 passed; 0 failed  (test_layout)
test result: ok. 5 passed; 0 failed  (test_painting)

$ cargo run --release -- pages/demo.html pages/demo.css renders/demo.ppm 800 600
wrote renders/demo.ppm (800x600) from pages/demo.html + pages/demo.css
```

## What it actually teaches

- **A layout engine's "auto" isn't one value, it's a solved system.**
  `width: auto` and `margin: auto` interact -- CSS2.1 says exactly one
  equation must balance across seven quantities (two margins, two
  borders, two paddings, one width), and which of them get to be the
  free variable depends on which ones are `auto` in the source. I first
  wrote this as a chain of `if`s and kept missing cases (auto width *and*
  auto margins together; an explicit width that overflows the container).
  Brubeck's tutorial handles it as one `match` over
  `(width == auto, margin_left == auto, margin_right == auto)` with five
  arms -- turning "check every combination" from a set of nested
  conditionals I had to re-derive by hand into a table I could just read
  off the spec. `explicit_width_and_centering_margin_auto` and
  `width_wider_than_container_zeroes_auto_margins` in `test_layout.rs`
  pin the two trickiest arms (symmetric centering, and the
  "value that keeps the equation true is negative, and that's allowed"
  overconstrained case) so a future refactor can't quietly break either.

- **Borrowing `&mut self.dimensions` early blocks a later
  `self.get_style_node()`, even though the two don't alias.** The
  compiler doesn't know that; it only sees a live `&mut` on `self` and a
  later `&self` call, so `calculate_block_position` didn't compile until
  I read every style value I needed *before* taking `let d = &mut
  self.dimensions`. This is the standard "front-load the reads, then
  mutate" shape you end up reaching for constantly once you're writing
  tree-mutation code in Rust instead of a GC'd language -- the same code
  shape in C++ would have compiled without complaint and just silently
  used whatever `dimensions` held at the time, aliasing bug included.

- **Style-cascade order is a sort key, not a special case.** I expected
  "higher specificity wins, ties go to source order" to need two
  separate rules (compare specificity; if equal, compare position). It
  doesn't: `matching_rules` already visits the stylesheet's rules in
  source order, so a plain `sort_by_key(specificity)` -- a *stable* sort
  -- preserves that source order automatically among equal keys. Rust's
  guarantee that `sort_by_key` is stable is what makes this true;
  swapping in an unstable sort would silently break
  `equal_specificity_ties_broken_by_source_order` in `test_style.rs`
  roughly half the time, which is exactly the kind of bug that survives
  code review and fails a coin-flip in CI.

- **No floats or inline-block means "side by side" doesn't exist yet.**
  `pages/demo.html` puts `.sidebar` and `.main` next to each other in the
  markup expecting a two-column layout, and `renders/demo.png` shows them
  stacked instead -- every block box in this engine occupies the full
  width available to it and stacks vertically, because that's genuinely
  everything CSS2.1 normal flow does for block-level boxes without floats
  or positioning. Seeing the demo page *not* look like a real webpage was
  a useful, concrete way to feel the boundary of what "just implement
  block layout" actually covers, versus what the extra tutorial chapters
  (floats, inline text flow) that I didn't build would add.
