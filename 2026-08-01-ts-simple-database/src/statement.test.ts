import { test } from "node:test";
import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { prepareStatement, executeStatement, PrepareError } from "./statement";
import { Table } from "./table";

function tempDbPath(): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "minidb-stmt-"));
  return path.join(dir, "test.db");
}

test("prepareStatement parses a well-formed insert", () => {
  const statement = prepareStatement("insert 1 bob bob@example.com");
  assert.deepEqual(statement, {
    type: "insert",
    row: { id: 1, username: "bob", email: "bob@example.com" },
  });
});

test("prepareStatement rejects a negative id before it ever reaches the table", () => {
  assert.throws(() => prepareStatement("insert -1 bob bob@example.com"), PrepareError);
});

test("prepareStatement rejects wrong arg count", () => {
  assert.throws(() => prepareStatement("insert 1 bob"), PrepareError);
});

test("prepareStatement rejects an unrecognized keyword", () => {
  assert.throws(() => prepareStatement("delete 1"), PrepareError);
});

test("executeStatement select formats every row plus a trailing Executed.", () => {
  const table = Table.open(tempDbPath());
  executeStatement(prepareStatement("insert 1 bob bob@example.com"), table);
  const output = executeStatement(prepareStatement("select"), table);
  assert.equal(output, "(1, bob, bob@example.com)\nExecuted.");
  table.close();
});
