//! 1-bit sprites and pixel-exact collision.

/// A 1-bit bitmap: `data[y * w + x]` is true where the sprite has a pixel.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Sprite {
    pub w: usize,
    pub h: usize,
    pub data: Vec<bool>,
}

impl Sprite {
    /// Builds a sprite from rows of ASCII art: `#` is a pixel, anything else
    /// is empty. Panics if the rows are ragged, so a typo in the art fails
    /// loudly the first time the art is loaded.
    pub fn from_art(rows: &[&str]) -> Sprite {
        assert!(!rows.is_empty(), "sprite art has no rows");
        let w = rows[0].chars().count();
        let mut data = Vec::with_capacity(w * rows.len());
        for (i, row) in rows.iter().enumerate() {
            assert_eq!(
                row.chars().count(),
                w,
                "sprite art row {i} is {} wide, expected {w}: {row:?}",
                row.chars().count()
            );
            data.extend(row.chars().map(|c| c == '#'));
        }
        Sprite {
            w,
            h: rows.len(),
            data,
        }
    }

    /// True if the sprite has a pixel at local `(x, y)`. Out of range is empty.
    pub fn get(&self, x: i32, y: i32) -> bool {
        if x < 0 || y < 0 || x as usize >= self.w || y as usize >= self.h {
            return false;
        }
        self.data[y as usize * self.w + x as usize]
    }

    /// Clears the pixel at local `(x, y)`, if it is in range.
    pub fn clear(&mut self, x: i32, y: i32) {
        if x >= 0 && y >= 0 && (x as usize) < self.w && (y as usize) < self.h {
            self.data[y as usize * self.w + x as usize] = false;
        }
    }

    /// Number of set pixels.
    pub fn count(&self) -> usize {
        self.data.iter().filter(|&&p| p).count()
    }
}

/// Finds the first world pixel where sprite `a` drawn at `(ax, ay)` and
/// sprite `b` drawn at `(bx, by)` both have a pixel.
///
/// Bounding boxes are checked first because they are cheap and almost always
/// disjoint; only when they intersect do we walk the shared rectangle.
pub fn overlap_at(
    a: &Sprite,
    ax: i32,
    ay: i32,
    b: &Sprite,
    bx: i32,
    by: i32,
) -> Option<(i32, i32)> {
    let x0 = ax.max(bx);
    let y0 = ay.max(by);
    let x1 = (ax + a.w as i32).min(bx + b.w as i32);
    let y1 = (ay + a.h as i32).min(by + b.h as i32);
    if x0 >= x1 || y0 >= y1 {
        return None;
    }
    for y in y0..y1 {
        for x in x0..x1 {
            if a.get(x - ax, y - ay) && b.get(x - bx, y - by) {
                return Some((x, y));
            }
        }
    }
    None
}

/// True if the two placed sprites share at least one pixel.
pub fn overlap(a: &Sprite, ax: i32, ay: i32, b: &Sprite, bx: i32, by: i32) -> bool {
    overlap_at(a, ax, ay, b, bx, by).is_some()
}

/// An axis-aligned rectangle in screen pixels.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

impl Rect {
    /// The rectangle a sprite covers when drawn at `(x, y)`.
    pub fn of(s: &Sprite, x: i32, y: i32) -> Rect {
        Rect {
            x,
            y,
            w: s.w as i32,
            h: s.h as i32,
        }
    }

    /// True if the rectangles intersect. Used for bullet-vs-bullet, where
    /// thin zig-zag sprites would otherwise slip past each other.
    pub fn intersects(self, o: Rect) -> bool {
        self.x < o.x + o.w && o.x < self.x + self.w && self.y < o.y + o.h && o.y < self.y + self.h
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn art_parses_and_counts() {
        let s = Sprite::from_art(&["#.#", ".#."]);
        assert_eq!((s.w, s.h, s.count()), (3, 2, 3));
        assert!(s.get(0, 0) && !s.get(1, 0) && s.get(1, 1));
        assert!(!s.get(-1, 0) && !s.get(3, 0) && !s.get(0, 2));
    }

    #[test]
    #[should_panic(expected = "row 1")]
    fn ragged_art_panics() {
        Sprite::from_art(&["##", "###"]);
    }

    #[test]
    fn boxes_touching_but_pixels_apart_do_not_overlap() {
        // Two diagonal halves of a 2x2 square: boxes coincide, pixels never do.
        let a = Sprite::from_art(&["#.", ".#"]);
        let b = Sprite::from_art(&[".#", "#."]);
        assert!(!overlap(&a, 0, 0, &b, 0, 0));
        assert_eq!(overlap_at(&a, 0, 0, &b, 1, 0), Some((1, 1)));
    }

    #[test]
    fn disjoint_boxes_never_overlap() {
        let a = Sprite::from_art(&["##", "##"]);
        assert!(!overlap(&a, 0, 0, &a, 2, 0));
        assert!(overlap(&a, 0, 0, &a, 1, 1));
        assert!(Rect::of(&a, 0, 0).intersects(Rect::of(&a, 1, 1)));
        assert!(!Rect::of(&a, 0, 0).intersects(Rect::of(&a, 2, 0)));
    }
}
