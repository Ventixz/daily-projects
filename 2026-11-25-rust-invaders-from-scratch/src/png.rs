//! A small PNG encoder, so screenshots need no image crate.
//!
//! A PNG is a signature plus chunks (length, type, data, CRC-32). The pixels
//! live in IDAT as a zlib stream. We compress with deflate's *fixed* Huffman
//! codes and a greedy LZ77 matcher: no tables to transmit, and pixel art is
//! so repetitive that this alone shrinks a frame by well over 90%.

/// Encodes 8-bit RGB pixels (`rgb.len() == w * h * 3`) as a PNG file.
pub fn encode_rgb(w: u32, h: u32, rgb: &[u8]) -> Vec<u8> {
    assert_eq!(
        rgb.len(),
        (w * h * 3) as usize,
        "pixel data does not match {w}x{h}"
    );
    // Each scanline is prefixed with filter type 0 ("none").
    let stride = (w * 3) as usize;
    let mut raw = Vec::with_capacity((stride + 1) * h as usize);
    for row in rgb.chunks(stride) {
        raw.push(0);
        raw.extend_from_slice(row);
    }

    let mut out = b"\x89PNG\r\n\x1a\n".to_vec();
    let mut ihdr = Vec::new();
    ihdr.extend_from_slice(&w.to_be_bytes());
    ihdr.extend_from_slice(&h.to_be_bytes());
    ihdr.extend_from_slice(&[8, 2, 0, 0, 0]); // 8-bit depth, truecolour, deflate, filter 0, no interlace
    chunk(&mut out, b"IHDR", &ihdr);
    chunk(&mut out, b"IDAT", &zlib(&raw));
    chunk(&mut out, b"IEND", &[]);
    out
}

fn chunk(out: &mut Vec<u8>, kind: &[u8; 4], data: &[u8]) {
    out.extend_from_slice(&(data.len() as u32).to_be_bytes());
    let start = out.len();
    out.extend_from_slice(kind);
    out.extend_from_slice(data);
    let crc = crc32(&out[start..]);
    out.extend_from_slice(&crc.to_be_bytes());
}

/// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320), bit by bit.
pub fn crc32(data: &[u8]) -> u32 {
    let mut crc = !0u32;
    for &b in data {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xEDB8_8320
            } else {
                crc >> 1
            };
        }
    }
    !crc
}

pub fn adler32(data: &[u8]) -> u32 {
    let (mut a, mut b) = (1u32, 0u32);
    for &x in data {
        a = (a + x as u32) % 65521;
        b = (b + a) % 65521;
    }
    (b << 16) | a
}

/// A zlib stream: 2-byte header, one fixed-Huffman deflate block, Adler-32.
pub fn zlib(data: &[u8]) -> Vec<u8> {
    let mut w = BitWriter::default();
    w.out.extend_from_slice(&[0x78, 0x01]); // deflate, 32K window, no dictionary; (0x7801 % 31 == 0)
    w.bits(1, 1); // BFINAL
    w.bits(1, 2); // BTYPE = 01, fixed Huffman
    lz77(data, &mut w);
    w.symbol(256); // end of block
    w.flush();
    w.out.extend_from_slice(&adler32(data).to_be_bytes());
    w.out
}

const LEN_BASE: [u16; 29] = [
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
    163, 195, 227, 258,
];
const LEN_EXTRA: [u8; 29] = [
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
];
const DIST_BASE: [u16; 30] = [
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
];
const DIST_EXTRA: [u8; 30] = [
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13,
    13,
];
const WINDOW: usize = 32 * 1024;
const MAX_MATCH: usize = 258;

/// Greedy LZ77: remember the last position of every 3-byte prefix and take
/// the match there if it is at least 3 bytes long.
fn lz77(data: &[u8], w: &mut BitWriter) {
    let mut head = vec![usize::MAX; 1 << 15];
    let hash = |i: usize| {
        ((data[i] as usize) << 10 ^ (data[i + 1] as usize) << 5 ^ data[i + 2] as usize) & 0x7FFF
    };
    let mut i = 0;
    while i < data.len() {
        if i + 3 <= data.len() {
            let h = hash(i);
            let cand = head[h];
            head[h] = i;
            if cand != usize::MAX && i - cand <= WINDOW {
                let max = MAX_MATCH.min(data.len() - i);
                let len = (0..max)
                    .take_while(|&k| data[cand + k] == data[i + k])
                    .count();
                if len >= 3 {
                    w.length(len);
                    w.distance(i - cand);
                    for j in i + 1..i + len {
                        if j + 3 <= data.len() {
                            head[hash(j)] = j;
                        }
                    }
                    i += len;
                    continue;
                }
            }
        }
        w.symbol(data[i] as u16);
        i += 1;
    }
}

#[derive(Default)]
struct BitWriter {
    out: Vec<u8>,
    acc: u64,
    n: u32,
}

impl BitWriter {
    /// Deflate packs ordinary values least-significant bit first.
    fn bits(&mut self, value: u32, count: u32) {
        self.acc |= (value as u64) << self.n;
        self.n += count;
        while self.n >= 8 {
            self.out.push(self.acc as u8);
            self.acc >>= 8;
            self.n -= 8;
        }
    }

    /// Huffman codes are defined most-significant bit first, so reverse them.
    fn code(&mut self, code: u32, len: u32) {
        let rev = code.reverse_bits() >> (32 - len);
        self.bits(rev, len);
    }

    /// A literal/length symbol in the fixed code (RFC 1951, 3.2.6).
    fn symbol(&mut self, sym: u16) {
        let s = sym as u32;
        match sym {
            0..=143 => self.code(0x30 + s, 8),
            144..=255 => self.code(0x190 + s - 144, 9),
            256..=279 => self.code(s - 256, 7),
            _ => self.code(0xC0 + s - 280, 8),
        }
    }

    fn length(&mut self, len: usize) {
        let i = LEN_BASE.iter().rposition(|&b| b as usize <= len).unwrap();
        self.symbol(257 + i as u16);
        self.bits((len - LEN_BASE[i] as usize) as u32, LEN_EXTRA[i] as u32);
    }

    fn distance(&mut self, d: usize) {
        let i = DIST_BASE.iter().rposition(|&b| b as usize <= d).unwrap();
        self.code(i as u32, 5);
        self.bits((d - DIST_BASE[i] as usize) as u32, DIST_EXTRA[i] as u32);
    }

    fn flush(&mut self) {
        if self.n > 0 {
            self.out.push(self.acc as u8);
            self.acc = 0;
            self.n = 0;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checksums_match_known_values() {
        assert_eq!(crc32(b"IEND"), 0xAE42_6082); // the CRC every PNG ends with
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926); // the standard check value
        assert_eq!(adler32(b"Wikipedia"), 0x11E6_0398);
    }

    #[test]
    fn length_258_uses_its_own_code() {
        // 258 has a dedicated symbol (285) with no extra bits, not 227 + 31.
        let i = LEN_BASE.iter().rposition(|&b| b as usize <= 258).unwrap();
        assert_eq!((257 + i, LEN_EXTRA[i]), (285, 0));
    }

    #[test]
    fn png_layout_is_well_formed() {
        let png = encode_rgb(2, 2, &[255; 12]);
        assert!(png.starts_with(b"\x89PNG\r\n\x1a\n"));
        assert_eq!(&png[12..16], b"IHDR");
        assert!(png.ends_with(&[0, 0, 0, 0, b'I', b'E', b'N', b'D', 0xAE, 0x42, 0x60, 0x82]));
    }

    #[test]
    fn repetitive_data_compresses() {
        let data = vec![7u8; 100_000];
        assert!(zlib(&data).len() < 2_000);
    }
}
