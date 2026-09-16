// Two different string forms of a number, for two different audiences.
//
// formatDisplay(n) is for the screen once a calculation is finalized (after
// `=`). It cleans up float noise (0.1+0.2 -> "0.3", not
// "0.30000000000000004") and is allowed to fall back to exponential
// notation for extreme magnitudes, because nothing re-parses this string.
//
// formatForExpression(n) is for splicing a number *back into* an editable
// expression string (used by percent and toggle-sign, which round-trip
// through the tokenizer). It must never emit exponential notation, because
// the tokenizer doesn't understand "e" -- a number that can't be re-typed
// isn't a number this calculator can keep working with.

function trimTrailingZeros(str) {
  if (!str.includes(".")) return str;
  str = str.replace(/0+$/, "");
  str = str.replace(/\.$/, "");
  return str;
}

function formatDisplay(n) {
  if (Object.is(n, -0)) n = 0;
  if (!Number.isFinite(n)) return "Error";
  if (n === 0) return "0";

  const precise = Number(n.toPrecision(12));
  const str = precise.toString();

  if (str.includes("e")) {
    const [mantissa, exp] = str.split("e");
    return `${trimTrailingZeros(mantissa)}e${exp}`;
  }
  return trimTrailingZeros(str);
}

function formatForExpression(n) {
  if (Object.is(n, -0)) n = 0;
  if (!Number.isFinite(n)) return "0";

  // Below 1e-9, toPrecision(12) would switch to exponential; toFixed(20)
  // doesn't, so it's the one that keeps the result re-tokenizable (the
  // tokenizer has no idea what "e" means). The cost, documented in
  // LEARNING.md, is a long plain decimal instead of scientific notation.
  if (n !== 0 && Math.abs(n) < 1e-9) {
    return trimTrailingZeros(n.toFixed(20));
  }
  // Above 1e21, toFixed *also* falls back to exponential (per spec, once a
  // number needs 21+ integer digits it just calls ToString instead) --
  // there's no plain-decimal escape hatch left. That result goes back into
  // the expression as exponential text, which the tokenizer will reject on
  // the next `=`; session.js turns that into a graceful "Error" rather than
  // a crash, so this is a deliberate degrade, not a bug.
  const precise = Number(n.toPrecision(12));
  return trimTrailingZeros(precise.toString());
}

export { formatDisplay, formatForExpression };
