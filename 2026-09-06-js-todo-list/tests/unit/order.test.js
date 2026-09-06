import { test, assert, assertThrows } from "./harness.js";
import { keyBetween, initialKeys } from "../../src/core/order.js";

test("keyBetween(null, null) returns a starting key", () => {
  assert(keyBetween(null, null) === "5");
});

test("keyBetween(lo, null) is strictly greater than lo", () => {
  const lo = "5";
  const key = keyBetween(lo, null);
  assert(key > lo, `${key} should sort after ${lo}`);
});

test("keyBetween(null, hi) is strictly between empty and hi", () => {
  const hi = "5";
  const key = keyBetween(null, hi);
  assert(key < hi, `${key} should sort before ${hi}`);
  assert(key > "", `${key} should sort after the empty string`);
});

test("keyBetween(lo, hi) sorts strictly between them", () => {
  const key = keyBetween("1", "2");
  assert(key > "1" && key < "2", `expected "1" < ${key} < "2"`);
});

test("keyBetween throws when lo does not sort before hi", () => {
  assertThrows(() => keyBetween("5", "5"), "equal bounds should throw");
  assertThrows(() => keyBetween("6", "5"), "reversed bounds should throw");
});

test("initialKeys produces N strictly increasing keys", () => {
  const keys = initialKeys(20);
  assert(keys.length === 20);
  for (let i = 1; i < keys.length; i++) {
    assert(keys[i] > keys[i - 1], `key ${i} (${keys[i]}) should sort after key ${i - 1} (${keys[i - 1]})`);
  }
});

test("initialKeys(0) returns an empty array", () => {
  assert(initialKeys(0).length === 0);
});

// This is the test that a float-based midpoint ("(lo + hi) / 2") can't pass
// forever: repeatedly bisecting the same gap eventually lands lo and hi on
// adjacent floats, and the midpoint just returns lo or hi again. A digit
// string can always grow one more character instead.
test("repeatedly inserting at the same point never runs out of precision", () => {
  let lo = "1";
  const hi = "2";
  const keys = [];
  for (let i = 0; i < 500; i++) {
    const key = keyBetween(lo, hi);
    assert(key > lo && key < hi, `iteration ${i}: expected ${lo} < ${key} < ${hi}`);
    keys.push(key);
    lo = key;
  }
  assert(new Set(keys).size === keys.length, "all generated keys should be distinct");
  assert(lo.length > 50, "precision (key length) should keep growing instead of running out");
});

test("many random insertions preserve sort order matching insertion history", () => {
  // Simulate building a list by repeatedly inserting a new item at a random
  // position among the items placed so far, the way drag-to-reorder would.
  let keys = [keyBetween(null, null)];
  for (let i = 0; i < 200; i++) {
    const pos = Math.floor(Math.random() * (keys.length + 1));
    const lo = pos > 0 ? keys[pos - 1] : null;
    const hi = pos < keys.length ? keys[pos] : null;
    const key = keyBetween(lo, hi);
    keys.splice(pos, 0, key);
  }
  const sorted = [...keys].sort();
  assert(
    keys.every((k, i) => k === sorted[i]),
    "insertion order should already be sorted order",
  );
  assert(new Set(keys).size === keys.length, "no duplicate keys should be generated");
});
