'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { QUOTES, mulberry32, shuffle, QuoteMachine, tweetUrl } = require('./quotes.js');

test('shuffle keeps the same elements and does not mutate input', () => {
  const input = [1, 2, 3, 4, 5, 6];
  const out = shuffle(input, mulberry32(1));
  assert.deepStrictEqual(input, [1, 2, 3, 4, 5, 6]);
  assert.deepStrictEqual(out.slice().sort(), input);
});

test('shuffle is deterministic for a seed', () => {
  assert.deepStrictEqual(shuffle([1, 2, 3, 4, 5], mulberry32(7)), shuffle([1, 2, 3, 4, 5], mulberry32(7)));
});

test('a full cycle deals every quote exactly once', () => {
  const m = new QuoteMachine(QUOTES, mulberry32(3));
  const seen = QUOTES.map(() => m.next());
  assert.strictEqual(new Set(seen).size, QUOTES.length);
});

test('never repeats the same quote back to back across many cycles', () => {
  for (let seed = 0; seed < 50; seed++) {
    const m = new QuoteMachine(QUOTES, mulberry32(seed));
    let prev = null;
    for (let i = 0; i < QUOTES.length * 6; i++) {
      const q = m.next();
      assert.notStrictEqual(q, prev);
      prev = q;
    }
  }
});

test('single-quote machine works', () => {
  const m = new QuoteMachine([QUOTES[0]]);
  assert.strictEqual(m.next(), QUOTES[0]);
  assert.strictEqual(m.next(), QUOTES[0]);
});

test('empty quote list throws', () => {
  assert.throws(() => new QuoteMachine([]));
});

test('tweetUrl encodes text and author', () => {
  const url = tweetUrl({ text: 'a & b', author: 'Me' });
  assert.ok(url.includes(encodeURIComponent('"a & b" - Me')));
});
