// Pure append-only log of past calculations, capped so an all-day session
// doesn't grow the array (and the rendered list) without bound.

const MAX_ENTRIES = 50;

function addEntry(history, expr, result) {
  const next = [...history, { expr, result }];
  return next.length > MAX_ENTRIES ? next.slice(next.length - MAX_ENTRIES) : next;
}

export { addEntry, MAX_ENTRIES };
