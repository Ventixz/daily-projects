'use strict';
const { reducer } = require('./core');

// Usage: node cli.js "A,B,C" "A,B,A" ...   (entries, then one comma-separated vote list per round)
const [entriesArg = 'Trainspotting,28 Days Later,Sunshine', ...rounds] = process.argv.slice(2);
let s = reducer(undefined, { type: 'SET_ENTRIES', entries: entriesArg.split(',') });
s = reducer(s, { type: 'NEXT' });
let r = 0;
while (!s.winner && s.vote) {
  console.log('Vote:', s.vote.pair.join(' vs '));
  for (const e of (rounds[r++] || s.vote.pair[0]).split(',')) s = reducer(s, { type: 'VOTE', entry: e });
  console.log('Tally:', JSON.stringify(s.vote.tally));
  s = reducer(s, { type: 'NEXT' });
}
console.log('Winner:', s.winner);
