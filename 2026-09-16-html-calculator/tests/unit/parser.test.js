import { test, assert, assertEqual } from "./harness.js";
import { tokenize } from "../../src/core/tokenizer.js";
import { normalizeUnary, toRPN, tokensToExpression, lastBinaryOperation } from "../../src/core/parser.js";
import { evalRPN } from "../../src/core/evaluator.js";

function rpnValues(tokens) {
  return toRPN(tokens).map((t) => t.value);
}

test("normalizeUnary: leading '-' becomes a unary marker", () => {
  const norm = normalizeUnary(tokenize("-5"));
  assertEqual(
    norm.map((t) => t.value),
    ["u-", 5],
  );
});

test("normalizeUnary: '-' after a binary operator is also unary", () => {
  const norm = normalizeUnary(tokenize("3*-4"));
  assertEqual(
    norm.map((t) => t.value),
    [3, "*", "u-", 4],
  );
});

test("normalizeUnary: '-' after a number is binary subtraction", () => {
  const norm = normalizeUnary(tokenize("3-4"));
  assertEqual(
    norm.map((t) => t.value),
    [3, "-", 4],
  );
});

test("normalizeUnary: double negative is two unary minuses, not an error", () => {
  const norm = normalizeUnary(tokenize("--5"));
  assertEqual(
    norm.map((t) => t.value),
    ["u-", "u-", 5],
  );
});

test("toRPN: multiplication binds tighter than addition", () => {
  // 3 + 4 * 2 -> 3 4 2 * +  (evaluates to 11, not 14)
  assertEqual(rpnValues(tokenize("3+4*2")), [3, 4, 2, "*", "+"]);
  assertEqual(evalRPN(toRPN(tokenize("3+4*2"))), 11);
});

test("toRPN: same-precedence operators are left-associative", () => {
  // 10 - 4 - 3 must be (10-4)-3 = 3, not 10-(4-3) = 9
  assertEqual(evalRPN(toRPN(tokenize("10-4-3"))), 3);
});

test("toRPN: unary minus binds tighter than any binary operator", () => {
  // 2 * -3 + 1 -> (2 * -3) + 1 = -5
  assertEqual(evalRPN(toRPN(tokenize("2*-3+1"))), -5);
});

test("toRPN: double negative evaluates back to the original sign", () => {
  assertEqual(evalRPN(toRPN(tokenize("--5"))), 5);
});

test("tokensToExpression: round-trips a normalized token list back to a string", () => {
  const tokens = tokenize("12+3");
  assertEqual(tokensToExpression(tokens), "12+3");
});

test("tokensToExpression: renormalizes float noise via formatForExpression", () => {
  const tokens = [{ type: "number", value: 3.5 }, { type: "op", value: "+" }, { type: "number", value: 1 }];
  assertEqual(tokensToExpression(tokens), "3.5+1");
});

test("lastBinaryOperation: splits at the final binary operator", () => {
  const info = lastBinaryOperation(tokenize("12+3*4"));
  assertEqual(info.operator, "*");
  assertEqual(info.operand, 4);
  assertEqual(evalRPN(toRPN(info.leftTokens)), 15); // 12+3
});

test("lastBinaryOperation: a trailing unary minus attaches to the operand, not the split point", () => {
  const info = lastBinaryOperation(tokenize("100+-25"));
  assertEqual(info.operator, "+");
  assertEqual(info.operand, -25);
});

test("lastBinaryOperation: returns null for a bare number (nothing to split)", () => {
  assertEqual(lastBinaryOperation(tokenize("42")), null);
});

test("lastBinaryOperation: returns null for a dangling trailing operator", () => {
  assertEqual(lastBinaryOperation(tokenize("12+")), null);
});
