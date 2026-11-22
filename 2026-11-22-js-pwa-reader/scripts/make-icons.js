// Generates the PWA icons with no image library: draw into an RGBA buffer,
// then write the PNG by hand (signature, IHDR, IDAT via zlib, IEND, CRC-32).
// Run with `npm run icons`. The output is committed under public/icons/.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const OUT = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'public', 'icons');

const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
export function encodePng(width, height, rgba) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // colour type: RGBA
  // Each scanline starts with a filter-type byte. 0 means no filter.
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y++) rgba.copy(raw, y * (width * 4 + 1) + 1, y * width * 4, (y + 1) * width * 4);
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

const ACCENT = [0x1f, 0x4e, 0x5f, 255];
const PAPER = [0xf6, 0xf4, 0xef, 255];
const INK = [0x9f, 0xb8, 0xc0, 255];

// Draw in unit coordinates (0..1) so one design works at every size.
// `safe` shrinks the artwork toward the centre. Maskable icons must keep
// everything important inside the central 80% circle.
function draw(size, { maskable }) {
  const px = Buffer.alloc(size * size * 4);
  const safe = maskable ? 0.72 : 1;
  const set = (x, y, c) => c.forEach((v, i) => { px[(y * size + x) * 4 + i] = v; });
  const inRoundRect = (u, v, x0, y0, x1, y1, r) => {
    if (u < x0 || u > x1 || v < y0 || v > y1) return false;
    const cx = Math.min(Math.max(u, x0 + r), x1 - r);
    const cy = Math.min(Math.max(v, y0 + r), y1 - r);
    return (u - cx) ** 2 + (v - cy) ** 2 <= r * r;
  };
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const u = (x + 0.5) / size;
      const v = (y + 0.5) / size;
      // Map into the safe area for the artwork.
      const a = (u - 0.5) / safe + 0.5;
      const b = (v - 0.5) / safe + 0.5;
      let c = [0, 0, 0, 0];
      if (maskable || inRoundRect(u, v, 0, 0, 1, 1, 0.18)) c = ACCENT;
      if (inRoundRect(a, b, 0.24, 0.16, 0.76, 0.84, 0.05)) {
        c = PAPER;
        for (const [ly, lw] of [[0.3, 0.4], [0.42, 0.4], [0.54, 0.4], [0.66, 0.26]]) {
          if (inRoundRect(a, b, 0.32, ly - 0.025, 0.32 + lw, ly + 0.025, 0.02)) c = INK;
        }
      }
      set(x, y, c);
    }
  }
  return encodePng(size, size, px);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  fs.mkdirSync(OUT, { recursive: true });
  fs.writeFileSync(path.join(OUT, 'icon-192.png'), draw(192, { maskable: false }));
  fs.writeFileSync(path.join(OUT, 'icon-512.png'), draw(512, { maskable: false }));
  fs.writeFileSync(path.join(OUT, 'maskable-512.png'), draw(512, { maskable: true }));
  console.log(`wrote icons to ${OUT}`);
}
