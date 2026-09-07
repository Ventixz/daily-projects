import { test, assert, assertEqual } from "./harness.js";
import { TtlCache } from "../../src/core/cache.js";

test("TtlCache: normalizes case and surrounding/interior whitespace", () => {
  const cache = new TtlCache<number>(1000);
  cache.set("  Paris ", 1);
  assertEqual(cache.get("paris"), 1);
  assertEqual(cache.get("PARIS"), 1);
  assertEqual(cache.get(" pa ris"), undefined, "interior whitespace differences are still a different key");
});

test("TtlCache: get returns undefined once the entry has expired", () => {
  let now = 0;
  const cache = new TtlCache<number>(100, () => now);
  cache.set("paris", 1);
  now = 50;
  assertEqual(cache.get("paris"), 1);
  now = 150;
  assertEqual(cache.get("paris"), undefined);
});

test("TtlCache: getStale still returns an expired value", () => {
  let now = 0;
  const cache = new TtlCache<number>(100, () => now);
  cache.set("paris", 42);
  now = 150;
  assertEqual(cache.get("paris"), undefined);
  assertEqual(cache.getStale("paris"), 42);
});

test("TtlCache: getStale returns undefined when the key was never set", () => {
  const cache = new TtlCache<number>(1000);
  assertEqual(cache.getStale("nowhere"), undefined);
});

test("TtlCache: has() mirrors get()'s freshness check", () => {
  let now = 0;
  const cache = new TtlCache<number>(100, () => now);
  cache.set("paris", 1);
  assert(cache.has("paris"));
  now = 150;
  assert(!cache.has("paris"));
});
