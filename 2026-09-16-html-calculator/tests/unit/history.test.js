import { test, assertEqual } from "./harness.js";
import { addEntry, MAX_ENTRIES } from "../../src/core/history.js";

test("addEntry appends without mutating the original array", () => {
  const original = [];
  const next = addEntry(original, "1+1", "2");
  assertEqual(original, []);
  assertEqual(next, [{ expr: "1+1", result: "2" }]);
});

test("addEntry caps the log at MAX_ENTRIES, dropping the oldest", () => {
  let history = [];
  for (let i = 0; i < MAX_ENTRIES + 10; i++) {
    history = addEntry(history, `${i}+1`, `${i + 1}`);
  }
  assertEqual(history.length, MAX_ENTRIES);
  assertEqual(history[0].expr, "10+1"); // the first 10 entries were dropped
  assertEqual(history[history.length - 1].expr, `${MAX_ENTRIES + 9}+1`);
});
