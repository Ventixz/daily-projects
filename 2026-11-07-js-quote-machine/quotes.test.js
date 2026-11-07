'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { mulberry32, QUOTES, QuoteMachine, format, tweetUrl } = require('./quotes');

test('prng is deterministic and in [0,1)', () => {
  const a = mulberry32(42), b = mulberry32(42);
  for (let i = 0; i < 100; i++) { const x = a(); assert.strictEqual(x, b()); assert.ok(x >= 0 && x < 1); }
});

test('never repeats the same quote twice in a row', () => {
  const m = new QuoteMachine(QUOTES, mulberry32(1));
  let prev = m.next();
  for (let i = 0; i < 2000; i++) { const q = m.next(); assert.notStrictEqual(q, prev); prev = q; }
});

test('eventually shows every quote', () => {
  const m = new QuoteMachine(QUOTES, mulberry32(7));
  const seen = new Set();
  for (let i = 0; i < 500; i++) seen.add(m.next());
  assert.strictEqual(seen.size, QUOTES.length);
});

test('single quote list works; two quotes alternate', () => {
  assert.strictEqual(new QuoteMachine([QUOTES[0]]).next(), QUOTES[0]);
  const m = new QuoteMachine(QUOTES.slice(0, 2), mulberry32(3));
  const a = m.next(), b = m.next(), c = m.next();
  assert.notStrictEqual(a, b); assert.strictEqual(a, c);
});

test('empty list rejected', () => assert.throws(() => new QuoteMachine([])));

test('same seed gives same sequence', () => {
  const run = s => { const m = new QuoteMachine(QUOTES, mulberry32(s)); return Array.from({ length: 10 }, () => m.next().text); };
  assert.deepStrictEqual(run(5), run(5));
});

test('format and tweet url encode correctly', () => {
  const q = { text: 'a & b', author: 'X' };
  assert.strictEqual(format(q), '"a & b" — X');
  assert.ok(tweetUrl(q).includes('%26'));
});
