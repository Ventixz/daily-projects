//! A CPU framebuffer. Every frame is drawn here pixel by pixel, then handed
//! to the window (or the PNG encoder) in one go. No GPU, no sprites API:
//! drawing a sprite is a loop that writes a colour wherever its bit is set.

use crate::sprite::Sprite;

/// Packs a colour the way minifb expects: `0x00RRGGBB`.
pub const fn rgb(r: u8, g: u8, b: u8) -> u32 {
    ((r as u32) << 16) | ((g as u32) << 8) | b as u32
}

pub struct Buffer {
    pub w: usize,
    pub h: usize,
    pub px: Vec<u32>,
}

impl Buffer {
    pub fn new(w: usize, h: usize) -> Buffer {
        Buffer {
            w,
            h,
            px: vec![0; w * h],
        }
    }

    pub fn clear(&mut self, color: u32) {
        self.px.fill(color);
    }

    pub fn get(&self, x: i32, y: i32) -> Option<u32> {
        if x < 0 || y < 0 || x as usize >= self.w || y as usize >= self.h {
            return None;
        }
        Some(self.px[y as usize * self.w + x as usize])
    }

    pub fn set(&mut self, x: i32, y: i32, color: u32) {
        if x >= 0 && y >= 0 && (x as usize) < self.w && (y as usize) < self.h {
            self.px[y as usize * self.w + x as usize] = color;
        }
    }

    pub fn fill_rect(&mut self, x: i32, y: i32, w: i32, h: i32, color: u32) {
        for yy in y.max(0)..(y + h).min(self.h as i32) {
            for xx in x.max(0)..(x + w).min(self.w as i32) {
                self.px[yy as usize * self.w + xx as usize] = color;
            }
        }
    }

    /// Draws the set pixels of `s` with its top-left at `(x, y)`, clipped to
    /// the buffer. Unset pixels are transparent.
    pub fn draw_sprite(&mut self, s: &Sprite, x: i32, y: i32, color: u32) {
        for sy in 0..s.h {
            for sx in 0..s.w {
                if s.data[sy * s.w + sx] {
                    self.set(x + sx as i32, y + sy as i32, color);
                }
            }
        }
    }

    /// The pixels as packed RGB bytes, row by row (for image encoders).
    pub fn to_rgb_bytes(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(self.px.len() * 3);
        for &p in &self.px {
            out.extend_from_slice(&[(p >> 16) as u8, (p >> 8) as u8, p as u8]);
        }
        out
    }

    /// A binary PPM (P6): the simplest image format there is.
    pub fn to_ppm(&self) -> Vec<u8> {
        let mut out = format!("P6\n{} {}\n255\n", self.w, self.h).into_bytes();
        out.extend(self.to_rgb_bytes());
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sprites_clip_at_every_edge() {
        let mut b = Buffer::new(4, 4);
        let s = Sprite::from_art(&["##", "##"]);
        for (x, y) in [(-1, -1), (3, 3), (-1, 3), (3, -1)] {
            b.draw_sprite(&s, x, y, 7);
        }
        assert_eq!(b.px.iter().filter(|&&p| p == 7).count(), 4); // one pixel per corner
        assert_eq!(b.get(0, 0), Some(7));
        assert_eq!(b.get(4, 0), None);
    }

    #[test]
    fn ppm_has_header_and_rgb_payload() {
        let mut b = Buffer::new(2, 1);
        b.set(1, 0, rgb(1, 2, 3));
        let ppm = b.to_ppm();
        assert!(ppm.starts_with(b"P6\n2 1\n255\n"));
        assert_eq!(&ppm[ppm.len() - 6..], &[0, 0, 0, 1, 2, 3]);
    }
}
