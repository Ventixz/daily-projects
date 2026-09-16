// Turns a raw expression string ("12.5+3*-4") into a flat token list.
// Deliberately dumb: it knows nothing about unary minus, operator
// precedence, or calculator semantics -- that all lives in parser.js and
// session.js. A '-' is always tokenized as an operator here, never folded
// into the number that follows it.

const OPERATORS = new Set(["+", "-", "*", "/"]);

function tokenize(expr) {
  const tokens = [];
  let i = 0;
  while (i < expr.length) {
    const c = expr[i];
    if (c === " ") {
      i++;
      continue;
    }
    if (c >= "0" && c <= "9" || c === ".") {
      const start = i;
      while (i < expr.length && ((expr[i] >= "0" && expr[i] <= "9") || expr[i] === ".")) i++;
      const raw = expr.slice(start, i);
      if ((raw.match(/\./g) || []).length > 1) {
        throw new Error(`malformed number "${raw}"`);
      }
      if (raw === "." || raw === "") {
        throw new Error(`malformed number "${raw}"`);
      }
      tokens.push({ type: "number", value: Number(raw) });
      continue;
    }
    if (OPERATORS.has(c)) {
      tokens.push({ type: "op", value: c });
      i++;
      continue;
    }
    throw new Error(`unexpected character "${c}" at position ${i}`);
  }
  return tokens;
}

export { tokenize, OPERATORS };
