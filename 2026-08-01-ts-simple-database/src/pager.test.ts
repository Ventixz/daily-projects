import { test } from "node:test";
import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { Pager, PagerError, PAGE_SIZE, TABLE_MAX_PAGES } from "./pager";

function tempDbPath(): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "minidb-pager-"));
  return path.join(dir, "test.db");
}

test("getPage caches: the same page number returns the same Buffer instance", () => {
  const pager = Pager.open(tempDbPath());
  const a = pager.getPage(0);
  const b = pager.getPage(0);
  assert.equal(a, b);
  pager.close();
});

test("getPage rejects a page number beyond TABLE_MAX_PAGES", () => {
  const pager = Pager.open(tempDbPath());
  assert.throws(() => pager.getPage(TABLE_MAX_PAGES), PagerError);
  pager.close();
});

test("a flushed page is readable by a fresh Pager over the same file", () => {
  const dbPath = tempDbPath();

  const writer = Pager.open(dbPath);
  const page = writer.getPage(0);
  page.write("hello page zero");
  writer.flushPage(0, PAGE_SIZE);
  writer.close();

  const reader = Pager.open(dbPath);
  const reread = reader.getPage(0);
  assert.equal(reread.toString("utf8", 0, "hello page zero".length), "hello page zero");
  reader.close();
});
