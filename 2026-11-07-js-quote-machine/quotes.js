'use strict';

// Small seedable PRNG (mulberry32) so behaviour is reproducible and testable.
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

const QUOTES = [
  { text: 'Programs must be written for people to read, and only incidentally for machines to execute.', author: 'Harold Abelson' },
  { text: 'Premature optimization is the root of all evil.', author: 'Donald Knuth' },
  { text: 'Simplicity is prerequisite for reliability.', author: 'Edsger W. Dijkstra' },
  { text: 'Make it work, make it right, make it fast.', author: 'Kent Beck' },
  { text: 'The most important property of a program is whether it accomplishes the intention of its user.', author: 'C.A.R. Hoare' },
  { text: 'Talk is cheap. Show me the code.', author: 'Linus Torvalds' },
];

// Picks random quotes but never the same one twice in a row.
class QuoteMachine {
  constructor(quotes = QUOTES, rng = Math.random) {
    if (!Array.isArray(quotes) || quotes.length === 0) throw new Error('need at least one quote');
    this.quotes = quotes;
    this.rng = rng;
    this.lastIndex = -1;
  }

  next() {
    const n = this.quotes.length;
    if (n === 1) { this.lastIndex = 0; return this.quotes[0]; }
    let i;
    if (this.lastIndex < 0) {
      i = Math.floor(this.rng() * n);
    } else {
      // Choose among the other n-1 quotes, then skip over the last index.
      i = Math.floor(this.rng() * (n - 1));
      if (i >= this.lastIndex) i += 1;
    }
    this.lastIndex = i;
    return this.quotes[i];
  }
}

function format(q) {
  return `"${q.text}" — ${q.author}`;
}

function tweetUrl(q) {
  return 'https://twitter.com/intent/tweet?text=' + encodeURIComponent(format(q));
}

if (typeof module !== 'undefined') module.exports = { mulberry32, QUOTES, QuoteMachine, format, tweetUrl };
