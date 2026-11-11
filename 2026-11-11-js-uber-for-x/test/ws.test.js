'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { acceptKey, encodeFrame, decodeFrame, OP } = require('../ws');

test('accept key matches the RFC 6455 example', () => {
  assert.equal(acceptKey('dGhlIHNhbXBsZSBub25jZQ=='), 's3pPLMBiTxaQ9kYGzzhZRbK+xOo=');
});

test('frames round-trip at 7-, 16- and 64-bit lengths, masked and unmasked', () => {
  for (const size of [0, 5, 125, 126, 65535, 65536, 70000]) {
    const payload = Buffer.alloc(size, 0x61);
    for (const mask of [false, true]) {
      const frame = encodeFrame(OP.TEXT, payload, { mask });
      const out = decodeFrame(frame);
      assert.equal(out.length, frame.length);
      assert.equal(out.masked, mask);
      assert.equal(out.opcode, OP.TEXT);
      assert.ok(out.fin);
      assert.ok(out.payload.equals(payload), `size ${size} mask ${mask}`);
    }
  }
});

test('decodeFrame waits for a complete frame', () => {
  const frame = encodeFrame(OP.TEXT, Buffer.from('hello world'), { mask: true });
  for (let i = 0; i < frame.length; i++) assert.equal(decodeFrame(frame.subarray(0, i)), null);
  assert.equal(decodeFrame(frame).payload.toString(), 'hello world');
});

test('decodeFrame rejects oversized control frames and reserved bits', () => {
  const bigPing = encodeFrame(OP.PING, Buffer.alloc(126));
  assert.throws(() => decodeFrame(bigPing), /invalid control frame/);
  const rsv = Buffer.from([0xc1, 0x00]);
  assert.throws(() => decodeFrame(rsv), /reserved bits/);
});
