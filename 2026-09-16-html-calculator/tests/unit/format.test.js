import { test, assert, assertEqual } from "./harness.js";
import { formatDisplay, formatForExpression } from "../../src/core/format.js";

test("formatDisplay: cleans up classic float noise", () => {
  assertEqual(formatDisplay(0.1 + 0.2), "0.3");
});

test("formatDisplay: integers show with no trailing decimal", () => {
  assertEqual(formatDisplay(4), "4");
});

test("formatDisplay: negative zero displays as 0", () => {
  assertEqual(formatDisplay(-0), "0");
});

test("formatDisplay: non-finite values display as Error", () => {
  assertEqual(formatDisplay(Infinity), "Error");
  assertEqual(formatDisplay(-Infinity), "Error");
  assertEqual(formatDisplay(NaN), "Error");
});

test("formatDisplay: very large magnitudes fall back to exponential", () => {
  assertEqual(formatDisplay(1.23e21), "1.23e+21");
});

test("formatDisplay: repeating decimals are rounded to 12 significant digits", () => {
  assertEqual(formatDisplay(1 / 3), "0.333333333333");
});

test("formatForExpression: never emits exponential notation for a re-typeable number", () => {
  const str = formatForExpression(0.0000000001); // 1e-10
  assert(!str.includes("e"), `expected no exponential notation, got "${str}"`);
});

test("formatForExpression: trims float noise the same way formatDisplay does", () => {
  assertEqual(formatForExpression(3.5), "3.5");
});

test("formatForExpression: negative zero normalizes to 0", () => {
  assertEqual(formatForExpression(-0), "0");
});
