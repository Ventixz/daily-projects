import { test, assert, assertEqual, assertThrows } from "./harness.js";
import { tokenize } from "../../src/core/tokenizer.js";

test("tokenize: number and operators", () => {
  assertEqual(tokenize("12+3"), [
    { type: "number", value: 12 },
    { type: "op", value: "+" },
    { type: "number", value: 3 },
  ]);
});

test("tokenize: decimal numbers", () => {
  assertEqual(tokenize("1.5*2.25"), [
    { type: "number", value: 1.5 },
    { type: "op", value: "*" },
    { type: "number", value: 2.25 },
  ]);
});

test("tokenize: leading minus is an operator token, not folded into the number", () => {
  assertEqual(tokenize("-5"), [
    { type: "op", value: "-" },
    { type: "number", value: 5 },
  ]);
});

test("tokenize: rejects a number with two decimal points", () => {
  assertThrows(() => tokenize("1.2.3"), "expected malformed-number error");
});

test("tokenize: rejects an unknown character", () => {
  assertThrows(() => tokenize("3^2"), "expected unexpected-character error");
});

test("tokenize: ignores spaces", () => {
  assertEqual(tokenize(" 1 + 2 "), [
    { type: "number", value: 1 },
    { type: "op", value: "+" },
    { type: "number", value: 2 },
  ]);
});

test("tokenize: empty string yields no tokens", () => {
  assertEqual(tokenize(""), []);
});

test("tokenize: a bare decimal point is malformed", () => {
  assertThrows(() => tokenize("."), "expected malformed-number error");
});
