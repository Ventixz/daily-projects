'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { padForKey, clockAngles, filterItems, highlight, withCommas } = require('./lib');

test('padForKey maps known keys and rejects unknown', () => {
  assert.equal(padForKey(65), 'clap');
  assert.equal(padForKey(76), 'tink');
  assert.equal(padForKey(13), null);
  assert.equal(padForKey('toString'), null); // no prototype leakage
});
test('clockAngles at 3:00:00 points hour hand straight down (180)', () => {
  const a = clockAngles(new Date(2026, 0, 1, 3, 0, 0));
  assert.equal(a.hour, 90 + 90);
  assert.equal(a.minute, 90);
  assert.equal(a.second, 90);
});
test('clockAngles is continuous: 6:30 hour hand is halfway between 6 and 7', () => {
  const a = clockAngles(new Date(2026, 0, 1, 6, 30, 0));
  assert.equal(a.hour, (6.5 / 12) * 360 + 90);
  assert.equal(a.minute, 180 + 90);
});
test('clockAngles wraps 24h to 12h', () => {
  assert.equal(clockAngles(new Date(2026, 0, 1, 15, 0, 0)).hour, clockAngles(new Date(2026, 0, 1, 3, 0, 0)).hour);
});
test('filterItems is case-insensitive and empty query returns nothing', () => {
  const cities = ['Boston', 'Austin', 'Houston', 'Denver'];
  assert.deepEqual(filterItems(cities, 'ST'), ['Boston', 'Austin', 'Houston']);
  assert.deepEqual(filterItems(cities, ''), []);
});
test('filterItems treats regex metacharacters literally', () => {
  assert.deepEqual(filterItems(['a.b', 'axb'], 'a.b'), ['a.b']);
  assert.doesNotThrow(() => filterItems(['x'], '(['));
});
test('highlight wraps every case-preserved match', () => {
  assert.equal(highlight('Austin', 'ST'), 'Au<span class="hl">st</span>in');
  assert.equal(highlight('banana', 'an'), 'b<span class="hl">an</span><span class="hl">an</span>a');
});
test('withCommas groups thousands', () => {
  assert.equal(withCommas(999), '999');
  assert.equal(withCommas(1234567), '1,234,567');
});
