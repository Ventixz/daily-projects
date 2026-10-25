'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { setEntries, next, vote, reducer, INITIAL_STATE } = require('./core');

const start = (...e) => next(setEntries(INITIAL_STATE, e));

test('setEntries stores entries and does not mutate', () => {
  const s = setEntries(INITIAL_STATE, ['a', 'b']);
  assert.deepEqual(s, { entries: ['a', 'b'], vote: undefined, winner: undefined });
  assert.deepEqual(INITIAL_STATE, {});
});

test('next takes first two entries into a vote', () => {
  const s = start('a', 'b', 'c');
  assert.deepEqual(s.vote.pair, ['a', 'b']);
  assert.deepEqual(s.entries, ['c']);
});

test('vote counts and ignores unknown entries', () => {
  let s = start('a', 'b');
  s = vote(vote(vote(s, 'a'), 'a'), 'b');
  assert.deepEqual(s.vote.tally, { a: 2, b: 1 });
  assert.equal(vote(s, 'zzz'), s);
  assert.equal(vote(INITIAL_STATE, 'a'), INITIAL_STATE);
});

test('state is deeply frozen', () => {
  const s = vote(start('a', 'b'), 'a');
  assert.throws(() => { 'use strict'; s.vote.tally.a = 9; }, TypeError);
  assert.throws(() => s.entries.push('x'), TypeError);
});

test('winner goes to the back of the queue, loser is dropped', () => {
  let s = vote(start('a', 'b', 'c'), 'b');
  s = next(s);
  assert.deepEqual(s.vote.pair, ['c', 'b']);
  assert.deepEqual(s.entries, []);
});

test('tie returns both entries to the queue', () => {
  let s = vote(vote(start('a', 'b', 'c'), 'a'), 'b');
  s = next(s);
  assert.deepEqual(s.vote.pair, ['c', 'a']);
  assert.deepEqual(s.entries, ['b']);
});

test('final winner is declared and vote cleared', () => {
  const s = next(vote(start('a', 'b'), 'b'));
  assert.equal(s.winner, 'b');
  assert.equal(s.vote, undefined);
  assert.equal(s.entries, undefined);
});

test('reducer handles actions, unknown types and initial state', () => {
  assert.deepEqual(reducer(undefined, { type: '??' }), {});
  const s = [{ type: 'SET_ENTRIES', entries: ['x', 'y'] }, { type: 'NEXT' }, { type: 'VOTE', entry: 'x' }, { type: 'NEXT' }]
    .reduce(reducer, undefined);
  assert.equal(s.winner, 'x');
});
