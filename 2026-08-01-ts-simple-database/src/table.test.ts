import { test } from "node:test";
import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { Table, TableError, TABLE_MAX_ROWS, ROWS_PER_PAGE } from "./table";

function tempDbPath(): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "minidb-table-"));
  return path.join(dir, "test.db");
}

test("insert then selectAll returns rows in insertion order", () => {
  const table = Table.open(tempDbPath());
  table.insert({ id: 1, username: "alice", email: "alice@example.com" });
  table.insert({ id: 2, username: "bob", email: "bob@example.com" });

  const rows = table.selectAll();
  assert.deepEqual(rows, [
    { id: 1, username: "alice", email: "alice@example.com" },
    { id: 2, username: "bob", email: "bob@example.com" },
  ]);
  table.close();
});

test("rows survive a close and reopen against the same file", () => {
  const dbPath = tempDbPath();

  const first = Table.open(dbPath);
  // Insert enough rows to span the page boundary, not just page zero.
  for (let i = 0; i < ROWS_PER_PAGE + 3; i++) {
    first.insert({ id: i, username: `user${i}`, email: `user${i}@example.com` });
  }
  first.close();

  const second = Table.open(dbPath);
  assert.equal(second.numRows, ROWS_PER_PAGE + 3);
  const rows = second.selectAll();
  assert.equal(rows.length, ROWS_PER_PAGE + 3);
  assert.deepEqual(rows[0], { id: 0, username: "user0", email: "user0@example.com" });
  assert.deepEqual(rows[ROWS_PER_PAGE + 2], {
    id: ROWS_PER_PAGE + 2,
    username: `user${ROWS_PER_PAGE + 2}`,
    email: `user${ROWS_PER_PAGE + 2}@example.com`,
  });
  second.close();
});

test("inserting beyond TABLE_MAX_ROWS throws Table full", () => {
  const table = Table.open(tempDbPath());
  for (let i = 0; i < TABLE_MAX_ROWS; i++) {
    table.insert({ id: i, username: "u", email: "u@e.com" });
  }
  assert.throws(
    () => table.insert({ id: TABLE_MAX_ROWS, username: "overflow", email: "o@e.com" }),
    TableError
  );
  table.close();
});
