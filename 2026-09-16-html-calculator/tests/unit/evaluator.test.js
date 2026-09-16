import { test, assert, assertEqual, assertThrows } from "./harness.js";
import { tokenize } from "../../src/core/tokenizer.js";
import { toRPN } from "../../src/core/parser.js";
import { evalRPN, applyBinary } from "../../src/core/evaluator.js";

function evaluate(expr) {
  return evalRPN(toRPN(tokenize(expr)));
}

test("applyBinary: the four operations", () => {
  assertEqual(applyBinary("+", 2, 3), 5);
  assertEqual(applyBinary("-", 2, 3), -1);
  assertEqual(applyBinary("*", 2, 3), 6);
  assertEqual(applyBinary("/", 6, 3), 2);
});

test("applyBinary: division by zero throws instead of returning Infinity", () => {
  assertThrows(() => applyBinary("/", 1, 0), "expected division-by-zero error");
});

test("evaluate: a full expression with mixed precedence", () => {
  assertEqual(evaluate("2+3*4-6/2"), 11);
});

test("evaluate: division by zero deep inside an expression still throws", () => {
  assertThrows(() => evaluate("5+1/0"), "expected division-by-zero error");
});

test("evalRPN: a malformed RPN stream (missing operand) throws rather than returning garbage", () => {
  assertThrows(() => evalRPN([{ type: "op", value: "+" }, { type: "number", value: 1 }]), "expected malformed-expression error");
});
