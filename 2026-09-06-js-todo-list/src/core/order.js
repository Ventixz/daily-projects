// Fractional indexing: order keys are decimal-digit strings ("", "5", "05", "512", ...)
// read as a fraction in [0, 1) with implied trailing zeros -- "5" means 0.5, "" means 0.
// Under that reading, plain string comparison ('<') already sorts them correctly: a
// missing digit is a zero, so "5" (0.5) sorts after "1" (0.1) and before "55" (0.55),
// exactly like the numbers they represent.
//
// The point of storing them as digit strings instead of floats is that inserting
// between two neighbors never runs out of precision. A float midpoint approach
// eventually collapses (a 64-bit double can't represent 2^-1075 vs 2^-1076
// distinctly), but a digit string can always grow one more digit. See
// order.test.js's "many insertions at the same point" case.

function digitsOf(key) {
  return key === null || key === undefined ? "" : key;
}

// Arbitrary-precision midpoint of two digit strings, both representing values
// in [0, 1), with a (as a fraction) strictly less than b. Grows the shared
// length until there's an integer strictly between them, then trims the
// trailing zeros that don't change the value.
function midpointDigits(a, b) {
  const len = Math.max(a.length, b.length, 1);
  const aPadded = a.padEnd(len, "0");
  const bPadded = b.padEnd(len, "0");
  const aInt = BigInt(aPadded);
  const bInt = BigInt(bPadded);

  if (bInt - aInt <= 1n) {
    // No integer strictly between them at this length -- both represent the
    // same value once you add one more digit of precision (aPadded+"0" and
    // bPadded+"0" don't change the fraction, just refine it), so recurse.
    return midpointDigits(aPadded + "0", bPadded + "0");
  }

  const midInt = aInt + (bInt - aInt) / 2n;
  const midStr = midInt.toString().padStart(len, "0");
  return midStr.replace(/0+$/, "");
}

// Returns a key strictly between `lo` and `hi`. `lo` may be null (meaning
// "before everything", i.e. 0); `hi` may be null (meaning "after everything",
// i.e. unbounded above). Throws if lo/hi are given in the wrong order or equal.
function keyBetween(lo, hi) {
  const loStr = digitsOf(lo);

  if (hi === null || hi === undefined) {
    if (lo === null || lo === undefined) return "5";
    // Unbounded above: appending any nonzero digit to `lo` is strictly
    // greater than `lo` (0.5100... > 0.51000...), and needs no search.
    return loStr + "5";
  }

  const hiStr = digitsOf(hi);
  if (loStr >= hiStr) {
    throw new Error(`keyBetween: lo (${JSON.stringify(lo)}) must sort before hi (${JSON.stringify(hi)})`);
  }
  return midpointDigits(loStr, hiStr);
}

// Convenience for building the first N keys of a fresh, empty list.
function initialKeys(count) {
  const keys = [];
  let prev = null;
  for (let i = 0; i < count; i++) {
    const key = keyBetween(prev, null);
    keys.push(key);
    prev = key;
  }
  return keys;
}

export { keyBetween, initialKeys };
