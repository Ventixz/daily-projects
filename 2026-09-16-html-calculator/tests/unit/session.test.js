import { test, assert, assertEqual } from "./harness.js";
import * as s from "../../src/core/session.js";

function press(state, ...keys) {
  for (const k of keys) {
    if (/^[0-9]$/.test(k)) state = s.pressDigit(state, k);
    else if (k === ".") state = s.pressDecimal(state);
    else if ("+-*/".includes(k)) state = s.pressOperator(state, k);
    else if (k === "%") state = s.pressPercent(state);
    else if (k === "=") state = s.pressEquals(state);
    else if (k === "AC") state = s.pressClear(state);
    else if (k === "CE") state = s.pressClearEntry(state);
    else if (k === "back") state = s.pressBackspace(state);
    else if (k === "+/-") state = s.pressToggleSign(state);
    else if (k === "M+" || k === "M-" || k === "MR" || k === "MC") state = s.pressMemory(state, k);
    else throw new Error(`unknown key "${k}"`);
  }
  return state;
}

test("digit entry builds up the expression", () => {
  const state = press(s.initialState(), "1", "2", "3");
  assertEqual(s.screenText(state), "123");
});

test("a lone leading zero is replaced by the next digit, not appended to", () => {
  const state = press(s.initialState(), "5");
  assertEqual(s.screenText(state), "5");
});

test("decimal point can only appear once per number segment", () => {
  const state = press(s.initialState(), "1", ".", "2", ".", "5");
  assertEqual(s.screenText(state), "1.25");
});

test("decimal point right after an operator seeds a leading '0.'", () => {
  const state = press(s.initialState(), "5", "+", ".", "2");
  assertEqual(s.screenText(state), "5+0.2");
});

test("pressing an operator right after another operator replaces it", () => {
  const state = press(s.initialState(), "5", "+", "*");
  assertEqual(s.screenText(state), "5*");
});

test("pressing '-' right after an operator adds a unary minus instead of replacing", () => {
  const state = press(s.initialState(), "5", "*", "-");
  assertEqual(s.screenText(state), "5*-");
});

test("pressing '-' again once a unary minus is pending is a no-op", () => {
  const state = press(s.initialState(), "5", "*", "-", "-");
  assertEqual(s.screenText(state), "5*-");
});

test("pressing a non-minus operator after a pending unary minus replaces the whole pair", () => {
  const state = press(s.initialState(), "5", "*", "-", "/");
  assertEqual(s.screenText(state), "5/");
});

test("= respects operator precedence", () => {
  const state = press(s.initialState(), "3", "+", "4", "*", "2", "=");
  assertEqual(s.screenText(state), "11");
});

test("= with a trailing operator and no operand is a no-op", () => {
  const state = press(s.initialState(), "3", "+", "=");
  assertEqual(s.screenText(state), "3+");
});

test("chaining continues from the previous result when an operator is pressed after =", () => {
  const state = press(s.initialState(), "3", "+", "4", "=", "*", "2", "=");
  assertEqual(s.screenText(state), "14"); // (3+4)=7, 7*2=14
});

test("typing a fresh digit after = starts a brand new calculation", () => {
  const state = press(s.initialState(), "3", "+", "4", "=", "9");
  assertEqual(s.screenText(state), "9");
});

test("repeat-= reapplies the last operator/operand pair", () => {
  const state = press(s.initialState(), "5", "+", "3", "=", "=");
  assertEqual(s.screenText(state), "11"); // 8, then +3 again -> 11
});

test("repeat-= twice keeps reapplying", () => {
  const state = press(s.initialState(), "5", "+", "3", "=", "=", "=");
  assertEqual(s.screenText(state), "14"); // 8, 11, 14
});

test("repeat-= is a no-op when the last calculation had no operator to repeat", () => {
  const state = press(s.initialState(), "4", "2", "=", "=");
  assertEqual(s.screenText(state), "42");
});

test("division by zero sets the error state", () => {
  const state = press(s.initialState(), "5", "/", "0", "=");
  assertEqual(s.screenText(state), "Error");
});

test("any digit press clears the error state and starts fresh", () => {
  const state = press(s.initialState(), "5", "/", "0", "=", "7");
  assertEqual(s.screenText(state), "7");
});

test("percent after '+' is a percentage of the first operand (additive context)", () => {
  const state = press(s.initialState(), "1", "0", "0", "+", "1", "0", "%");
  assertEqual(s.screenText(state), "100+10"); // 10% of 100 == 10
});

test("percent completing an additive expression matches hand computation", () => {
  const state = press(s.initialState(), "1", "0", "0", "+", "1", "0", "%", "=");
  assertEqual(s.screenText(state), "110");
});

test("percent after '*' is a plain multiplier (multiplicative context), not 'percent of'", () => {
  const state = press(s.initialState(), "1", "0", "0", "*", "1", "0", "%", "=");
  assertEqual(s.screenText(state), "10"); // 100 * (10/100) == 10, not 100*10% "of 100"
});

test("standalone percent with no preceding operator divides by 100", () => {
  const state = press(s.initialState(), "5", "0", "%");
  assertEqual(s.screenText(state), "0.5");
});

test("toggle sign on a plain number adds a leading unary minus", () => {
  const state = press(s.initialState(), "4", "2", "+/-");
  assertEqual(s.screenText(state), "-42");
});

test("toggle sign twice returns to the original (positive) value", () => {
  const state = press(s.initialState(), "4", "2", "+/-", "+/-");
  assertEqual(s.screenText(state), "42");
});

test("toggle sign only affects the trailing number, not the whole expression", () => {
  const state = press(s.initialState(), "5", "+", "3", "+/-");
  assertEqual(s.screenText(state), "5+-3");
});

test("toggle sign re-serializes the number, collapsing a trailing-zero decimal in progress", () => {
  const state = press(s.initialState(), "3", ".", "5", "0", "+/-");
  assertEqual(s.screenText(state), "-3.5"); // not "-3.50" -- see LEARNING.md
});

test("backspace removes one character at a time", () => {
  const state = press(s.initialState(), "1", "2", "3", "back");
  assertEqual(s.screenText(state), "12");
});

test("backspace right after = clears to a fresh start rather than editing the result", () => {
  const state = press(s.initialState(), "4", "+", "4", "=", "back");
  assertEqual(s.screenText(state), "0");
});

test("clear-entry removes only the number currently being typed, keeping earlier terms", () => {
  const state = press(s.initialState(), "1", "2", "+", "3", "4", "CE");
  assertEqual(s.screenText(state), "12+");
});

test("AC resets the calculation but keeps memory and history", () => {
  let state = press(s.initialState(), "5", "M+", "1", "+", "1", "=");
  state = s.pressClear(state);
  assertEqual(s.screenText(state), "0");
  assertEqual(state.memory, 5);
  assertEqual(state.history.length, 1);
});

test("M+ adds the currently displayed value to memory, MR recalls it", () => {
  let state = press(s.initialState(), "1", "0", "M+");
  state = press(state, "AC", "5", "M+");
  state = s.pressMemory(state, "MR");
  assertEqual(s.screenText(state), "15");
});

test("MC clears memory back to zero", () => {
  let state = press(s.initialState(), "1", "0", "M+", "MC");
  assertEqual(state.memory, 0);
});

test("M- subtracts the current value from memory", () => {
  let state = press(s.initialState(), "1", "0", "M+");
  state = press(state, "AC", "4", "M-");
  assertEqual(state.memory, 6);
});

test("recalling memory (MR) is ready to chain like a result would be", () => {
  let state = press(s.initialState(), "7", "M+", "MR", "+", "3", "=");
  assertEqual(s.screenText(state), "10");
});

test("every completed calculation is logged to history", () => {
  const state = press(s.initialState(), "2", "+", "2", "=");
  assertEqual(state.history, [{ expr: "2+2", result: "4" }]);
});
