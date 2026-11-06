'use strict';

const QUOTES = [
  { text: 'Programs must be written for people to read, and only incidentally for machines to execute.', author: 'Harold Abelson' },
  { text: 'Simplicity is prerequisite for reliability.', author: 'Edsger W. Dijkstra' },
  { text: 'Premature optimization is the root of all evil.', author: 'Donald Knuth' },
  { text: 'Make it work, make it right, make it fast.', author: 'Kent Beck' },
  { text: 'The best way to predict the future is to invent it.', author: 'Alan Kay' },
  { text: 'Talk is cheap. Show me the code.', author: 'Linus Torvalds' },
  { text: 'First, solve the problem. Then, write the code.', author: 'John Johnson' },
  { text: 'Debugging is twice as hard as writing the code in the first place.', author: 'Brian Kernighan' },
];

// mulberry32: small seedable PRNG so shuffles are reproducible in tests.
function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// Fisher-Yates shuffle (returns a new array).
function shuffle(items, rng = Math.random) {
  const out = items.slice();
  for (let i = out.length - 1; i > 0; i--) {
    const j = Math.floor(rng() * (i + 1));
    [out[i], out[j]] = [out[j], out[i]];
  }
  return out;
}

// Deals every quote once per cycle (no repeats until the deck is exhausted),
// and never repeats the last card across a reshuffle boundary.
class QuoteMachine {
  constructor(quotes = QUOTES, rng = Math.random) {
    if (!quotes.length) throw new Error('need at least one quote');
    this.quotes = quotes;
    this.rng = rng;
    this.deck = [];
    this.last = null;
  }

  next() {
    if (this.deck.length === 0) {
      this.deck = shuffle(this.quotes, this.rng);
      if (this.quotes.length > 1 && this.deck[this.deck.length - 1] === this.last) {
        // last card is dealt first (pop), so swap it away from the end
        [this.deck[0], this.deck[this.deck.length - 1]] = [this.deck[this.deck.length - 1], this.deck[0]];
      }
    }
    this.last = this.deck.pop();
    return this.last;
  }
}

function tweetUrl(q) {
  return 'https://twitter.com/intent/tweet?text=' + encodeURIComponent(`"${q.text}" - ${q.author}`);
}

if (typeof module !== 'undefined') module.exports = { QUOTES, mulberry32, shuffle, QuoteMachine, tweetUrl };
