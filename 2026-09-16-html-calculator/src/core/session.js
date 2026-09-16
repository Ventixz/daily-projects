// The calculator's state machine. Pure reducers only -- no DOM, no
// `Date.now()`, nothing app.js couldn't fake in a test. Each function takes
// the current state and returns a new one; render.js decides how to draw
// it, app.js decides which one a button click calls.
//
// State shape:
//   expression   - the raw string being built ("12+3", "-4.5"), using the
//                  tokenizer's own operator characters (+-*/), never
//                  display symbols (×÷) -- those are a render.js concern.
//   justEvaluated- true right after `=` (or MR): `expression` currently
//                  holds a *committed* number, not something still being
//                  typed. Governs whether the next digit replaces it
//                  (fresh calculation) or an operator extends it (chained
//                  calculation continuing from the result).
//   error        - true after a division-by-zero or malformed expression.
//                  Screen shows "Error"; only AC/digit/decimal clear it.
//   lastOp       - {operator, operand} captured from the most recent `=`,
//                  for repeat-`=` (press `=` twice: "5+3=" -> 8, "=" -> 11).
//                  null when the last calculation had no binary operator to
//                  repeat (a bare number "42=").
//   memory       - M+/M-/MR/MC register. Survives AC (deliberately --
//                  clearing the current calculation shouldn't wipe a
//                  number you stashed for later).
//   history      - append-only log of past calculations, capped by
//                  history.js. Also survives AC.
import { tokenize } from "./tokenizer.js";
import { toRPN, normalizeUnary, tokensToExpression, lastBinaryOperation } from "./parser.js";
import { evalRPN, applyBinary } from "./evaluator.js";
import { formatDisplay, formatForExpression } from "./format.js";
import { memoryAdd, memorySubtract } from "./memory.js";
import { addEntry } from "./history.js";

function initialState() {
  return { expression: "", justEvaluated: false, error: false, lastOp: null, memory: 0, history: [] };
}

function screenText(state) {
  if (state.error) return "Error";
  return state.expression === "" ? "0" : state.expression;
}

// The trailing run of non-operator characters is exactly the number
// currently being typed (a leading unary minus for that number is an
// operator character itself, so it's correctly excluded -- "3*-" has a
// trailing run of "", meaning "nothing typed yet after the sign").
function currentSegment(expr) {
  return expr.match(/[^+\-*/]*$/)[0];
}

function appendDigit(expr, d) {
  return currentSegment(expr) === "0" ? expr.slice(0, -1) + d : expr + d;
}

function appendDecimal(expr) {
  const segment = currentSegment(expr);
  if (segment.includes(".")) return expr;
  return segment === "" ? expr + "0." : expr + ".";
}

// Appends an operator to a raw expression string, applying the "one
// operator (plus at most one pending unary minus) at a time" rule button
// UIs need: pressing an operator after another operator replaces it,
// except '-' right after one, which *adds* a unary minus instead of
// replacing (so "3*" then "-" gives "3*-", ready for a negative operand).
// Returns null to mean "no-op" (e.g. '-' pressed when a unary minus is
// already pending).
function appendOperator(expr, op) {
  if (expr === "") return op === "-" ? "-" : "";

  const match = expr.match(/([+\-*/])([+\-*/]?)$/);
  if (!match) return expr + op; // ends in a number: plain append

  const [full, first, second] = match;
  const head = expr.slice(0, expr.length - full.length);
  if (!second) {
    return op === "-" ? head + first + "-" : head + op;
  }
  return op === "-" ? null : head + op;
}

function pressDigit(state, d) {
  if (state.error) return { ...state, expression: d, error: false, justEvaluated: false };
  if (state.justEvaluated) return { ...state, expression: d, justEvaluated: false, lastOp: null };
  return { ...state, expression: appendDigit(state.expression, d) };
}

function pressDecimal(state) {
  if (state.error) return { ...state, expression: "0.", error: false, justEvaluated: false };
  if (state.justEvaluated) return { ...state, expression: "0.", justEvaluated: false, lastOp: null };
  return { ...state, expression: appendDecimal(state.expression) };
}

function pressOperator(state, op) {
  if (state.error) return state;
  const base = state.justEvaluated ? { ...state, justEvaluated: false } : state;
  const appended = appendOperator(base.expression, op);
  if (appended === null) return base;
  return { ...base, expression: appended };
}

// Percent is contextual, not a grammar operator: "100+10%" means "10% of
// 100" (additive/subtractive context asks "percent of the first operand"),
// but "100*10%" means "10% as a plain multiplier" (multiplicative context
// doesn't have a natural "of what" other than itself). A standalone
// "50%" with no preceding operator just means 0.5. This function rewrites
// the trailing number in place and lets the generic tokenizer/parser
// re-read the result -- it never introduces a "%" token of its own.
function pressPercent(state) {
  if (state.error) return state;
  try {
    const tokens = tokenize(state.expression);
    if (tokens.length === 0) return state;

    const info = lastBinaryOperation(tokens);
    let newValue;
    let prefix;
    if (!info) {
      const last = tokens[tokens.length - 1];
      if (last.type !== "number") return state;
      newValue = evalRPN(toRPN(tokens)) / 100;
      prefix = "";
    } else {
      const { operator, operand, leftTokens } = info;
      const firstOperand = evalRPN(toRPN(leftTokens));
      newValue = operator === "+" || operator === "-" ? firstOperand * (operand / 100) : operand / 100;
      prefix = tokensToExpression(leftTokens) + operator;
    }
    return { ...state, expression: prefix + formatForExpression(newValue) };
  } catch {
    return state;
  }
}

// Toggling sign round-trips the trailing number through the tokenizer
// (tokenize -> normalizeUnary -> tokensToExpression) instead of doing raw
// string surgery, so it shares the exact same "what counts as this
// number's sign" logic as the parser. The tradeoff: it re-serializes the
// number via formatForExpression, so "3.50" becomes "-3.5", not "-3.50".
function pressToggleSign(state) {
  if (state.error) return state;
  try {
    const tokens = tokenize(state.expression);
    if (tokens.length === 0) return state;
    const normalized = normalizeUnary(tokens);
    const last = normalized[normalized.length - 1];
    if (last.type !== "number") return state;

    const hasLeadingMinus = normalized.length >= 2 && normalized[normalized.length - 2].value === "u-";
    const rebuilt = hasLeadingMinus
      ? normalized.slice(0, -2).concat([last])
      : normalized.slice(0, -1).concat([{ type: "op", value: "u-" }, last]);
    return { ...state, expression: tokensToExpression(rebuilt) };
  } catch {
    return state;
  }
}

function pressEquals(state) {
  if (state.error) return state;

  if (state.justEvaluated) {
    if (!state.lastOp) return state; // nothing to repeat (e.g. last calc was a bare number)
    try {
      const currentVal = evalRPN(toRPN(tokenize(state.expression)));
      const value = applyBinary(state.lastOp.operator, currentVal, state.lastOp.operand);
      const exprText = `${formatDisplay(currentVal)}${state.lastOp.operator}${formatDisplay(state.lastOp.operand)}`;
      return {
        ...state,
        expression: formatForExpression(value),
        justEvaluated: true,
        history: addEntry(state.history, exprText, formatDisplay(value)),
      };
    } catch {
      return { ...state, error: true, expression: "", justEvaluated: false, lastOp: null };
    }
  }

  try {
    const tokens = tokenize(state.expression);
    if (tokens.length === 0) return state;
    if (tokens[tokens.length - 1].type !== "number") return state; // dangling operator, no operand yet

    const value = evalRPN(toRPN(tokens));
    const lastOp = lastBinaryOperation(tokens);
    return {
      ...state,
      expression: formatForExpression(value),
      justEvaluated: true,
      lastOp,
      history: addEntry(state.history, state.expression, formatDisplay(value)),
    };
  } catch {
    return { ...state, error: true, expression: "", justEvaluated: false, lastOp: null };
  }
}

function pressClear(state) {
  return { expression: "", justEvaluated: false, error: false, lastOp: null, memory: state.memory, history: state.history };
}

function pressClearEntry(state) {
  if (state.error) return { ...state, error: false, expression: "" };
  if (state.justEvaluated) return { ...state, expression: "", justEvaluated: false };
  return { ...state, expression: state.expression.replace(/[^+\-*/]*$/, "") };
}

function pressBackspace(state) {
  if (state.error) return { ...state, error: false, expression: "" };
  if (state.justEvaluated) return { ...state, expression: "", justEvaluated: false };
  return { ...state, expression: state.expression.slice(0, -1) };
}

function currentValue(state) {
  if (state.error) return null;
  if (state.expression === "") return 0;
  try {
    return evalRPN(toRPN(tokenize(state.expression)));
  } catch {
    return null;
  }
}

function pressMemory(state, action) {
  if (action === "MC") return { ...state, memory: 0 };
  if (action === "MR") {
    if (state.error) return state;
    return { ...state, expression: formatForExpression(state.memory), justEvaluated: true, lastOp: null };
  }
  const value = currentValue(state);
  if (value === null) return state;
  const memory = action === "M+" ? memoryAdd(state.memory, value) : memorySubtract(state.memory, value);
  return { ...state, memory };
}

export {
  initialState,
  screenText,
  pressDigit,
  pressDecimal,
  pressOperator,
  pressPercent,
  pressToggleSign,
  pressEquals,
  pressClear,
  pressClearEntry,
  pressBackspace,
  pressMemory,
};
