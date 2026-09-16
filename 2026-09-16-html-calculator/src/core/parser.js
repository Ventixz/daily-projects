// Infix token list -> RPN, via the shunting-yard algorithm. Also owns the
// one piece of grammar the tokenizer deliberately doesn't know about: a
// '-' is unary (a sign, not subtraction) whenever it starts the expression
// or immediately follows another operator. "--5" is therefore two unary
// minuses applied to 5, not an error -- consistent, if not something a
// button UI can normally produce (see session.js's operator-press rules).
import { formatForExpression } from "./format.js";

const PRECEDENCE = { "u-": 4, "*": 3, "/": 3, "+": 2, "-": 2 };
const RIGHT_ASSOCIATIVE = new Set(["u-"]);

function normalizeUnary(tokens) {
  const out = [];
  let prevIsOperandBoundary = true; // start of input counts as a boundary
  for (const t of tokens) {
    if (t.type === "op" && t.value === "-" && prevIsOperandBoundary) {
      out.push({ type: "op", value: "u-" });
      // a unary minus doesn't itself satisfy the "need an operand" state --
      // another one right after it is still valid ("--5").
      prevIsOperandBoundary = true;
    } else {
      out.push(t);
      prevIsOperandBoundary = t.type === "op";
    }
  }
  return out;
}

function toRPN(tokens) {
  const normalized = normalizeUnary(tokens);
  const output = [];
  const opStack = [];

  for (const t of normalized) {
    if (t.type === "number") {
      output.push(t);
      continue;
    }
    // t.type === "op"
    while (
      opStack.length > 0 &&
      opStack[opStack.length - 1].type === "op" &&
      (PRECEDENCE[opStack[opStack.length - 1].value] > PRECEDENCE[t.value] ||
        (PRECEDENCE[opStack[opStack.length - 1].value] === PRECEDENCE[t.value] &&
          !RIGHT_ASSOCIATIVE.has(t.value)))
    ) {
      output.push(opStack.pop());
    }
    opStack.push(t);
  }
  while (opStack.length > 0) output.push(opStack.pop());

  if (output.length === 0) throw new Error("empty expression");
  return output;
}

// Re-serializes a (possibly already-normalized) token list back into a
// plain expression string. Numbers round-trip through formatForExpression,
// which means a number typed as "3.50" comes back out as "3.5" -- see
// LEARNING.md for why that's an accepted, deliberate side effect of reusing
// the tokenizer for percent/toggle-sign instead of doing string surgery.
function tokensToExpression(tokens) {
  return tokens
    .map((t) => {
      if (t.type === "number") return formatForExpression(t.value);
      return t.value === "u-" ? "-" : t.value; // 'u-' is an internal marker, not a real operator char
    })
    .join("");
}

// Splits a raw (non-normalized) token list at its last *binary* operator,
// evaluating the right-hand side (which the grammar guarantees is either a
// bare number or a unary-minus'd number). Returns null when there is no
// binary operator to split on -- a bare number, or an expression ending in
// a dangling operator with no operand yet. Used for repeat-`=` and for
// percent's "percent of what" context.
function lastBinaryOperation(tokens) {
  const normalized = normalizeUnary(tokens);
  for (let i = normalized.length - 1; i >= 0; i--) {
    const t = normalized[i];
    if (t.type !== "op" || t.value === "u-") continue;

    const leftTokens = normalized.slice(0, i);
    const rightTokens = normalized.slice(i + 1);
    if (leftTokens.length === 0 || rightTokens.length === 0) return null;

    let operand;
    if (rightTokens.length === 1 && rightTokens[0].type === "number") {
      operand = rightTokens[0].value;
    } else if (rightTokens.length === 2 && rightTokens[0].value === "u-" && rightTokens[1].type === "number") {
      operand = -rightTokens[1].value;
    } else {
      return null; // shouldn't happen for a grammar-valid expression
    }

    return { operator: t.value, operand, leftTokens };
  }
  return null;
}

export { normalizeUnary, toRPN, tokensToExpression, lastBinaryOperation };
