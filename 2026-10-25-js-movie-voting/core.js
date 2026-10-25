'use strict';
// Voting core: pure functions over immutable (deep-frozen) state, after the
// Full Stack Movie Voting App tutorial's tournament logic.

const deepFreeze = (o) => {
  if (o && typeof o === 'object' && !Object.isFrozen(o)) {
    Object.values(o).forEach(deepFreeze);
    Object.freeze(o);
  }
  return o;
};

const INITIAL_STATE = deepFreeze({});

// Load the entries to vote on; resets any previous tournament.
function setEntries(state, entries) {
  return deepFreeze({ ...state, entries: [...entries], vote: undefined, winner: undefined });
}

// Take the next two entries off the queue to vote on.
function next(state) {
  const entries = [...(state.entries || []), ...winnersOf(state.vote)];
  if (entries.length === 1) {
    const { vote, entries: _e, ...rest } = state;
    return deepFreeze({ ...rest, winner: entries[0] });
  }
  const [a, b, ...rest] = entries;
  return deepFreeze({ ...omit(state, 'winner'), vote: { pair: [a, b], tally: {} }, entries: rest });
}

function vote(state, entry) {
  if (!state.vote || !state.vote.pair.includes(entry)) return state;
  const tally = state.vote.tally;
  return deepFreeze({
    ...state,
    vote: { ...state.vote, tally: { ...tally, [entry]: (tally[entry] || 0) + 1 } },
  });
}

function winnersOf(v) {
  if (!v) return [];
  const [a, b] = v.pair;
  const x = v.tally[a] || 0;
  const y = v.tally[b] || 0;
  if (x > y) return [a];
  if (y > x) return [b];
  return [a, b]; // tie: both go back in the queue
}

function omit(o, k) {
  const { [k]: _, ...rest } = o;
  return rest;
}

// Redux-style reducer over {type, ...payload} actions.
function reducer(state = INITIAL_STATE, action) {
  switch (action.type) {
    case 'SET_ENTRIES': return setEntries(state, action.entries);
    case 'NEXT': return next(state);
    case 'VOTE': return vote(state, action.entry);
    default: return state;
  }
}

module.exports = { INITIAL_STATE, setEntries, next, vote, reducer, deepFreeze };
