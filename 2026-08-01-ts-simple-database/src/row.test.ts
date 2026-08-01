import { test } from "node:test";
import assert from "node:assert/strict";
import { serializeRow, deserializeRow, validateRow, RowError, ROW_SIZE } from "./row";

test("serialize/deserialize round-trips a row through a raw buffer", () => {
  const page = Buffer.alloc(ROW_SIZE * 2);
  serializeRow({ id: 7, username: "alice", email: "alice@example.com" }, page, ROW_SIZE);

  const row = deserializeRow(page, ROW_SIZE);
  assert.deepEqual(row, { id: 7, username: "alice", email: "alice@example.com" });
});

test("serializeRow zero-fills so a shorter second write erases the first", () => {
  const page = Buffer.alloc(ROW_SIZE);
  serializeRow({ id: 1, username: "averylongusername", email: "a@b.com" }, page, 0);
  serializeRow({ id: 2, username: "bo", email: "b@b.com" }, page, 0);

  const row = deserializeRow(page, 0);
  assert.deepEqual(row, { id: 2, username: "bo", email: "b@b.com" });
});

test("validateRow rejects a negative id", () => {
  assert.throws(
    () => validateRow({ id: -1, username: "a", email: "a@b.com" }),
    RowError
  );
});

test("validateRow rejects a username longer than the fixed column width", () => {
  const tooLong = "x".repeat(64);
  assert.throws(
    () => validateRow({ id: 1, username: tooLong, email: "a@b.com" }),
    RowError
  );
});
