import { test, assertEqual } from "./harness.js";
import { memoryAdd, memorySubtract, memoryClear } from "../../src/core/memory.js";

test("memoryAdd / memorySubtract accumulate", () => {
  let m = 0;
  m = memoryAdd(m, 5);
  m = memoryAdd(m, 2.5);
  m = memorySubtract(m, 1);
  assertEqual(m, 6.5);
});

test("memoryClear always returns 0 regardless of current value", () => {
  assertEqual(memoryClear(99), 0);
});
