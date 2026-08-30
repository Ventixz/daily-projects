import { test } from "node:test";
import assert from "node:assert/strict";
import { compilePath } from "./path";

test("matches a static path exactly", () => {
  const { regex, keys } = compilePath("/users");
  assert.deepEqual(keys, []);
  assert.ok(regex.test("/users"));
  assert.ok(!regex.test("/users/1"));
  assert.ok(!regex.test("/other"));
});

test("extracts a single param", () => {
  const { regex, keys } = compilePath("/users/:id");
  assert.deepEqual(keys, ["id"]);
  const m = regex.exec("/users/42");
  assert.ok(m);
  assert.equal(m![1], "42");
});

test("extracts multiple params in order", () => {
  const { regex, keys } = compilePath("/users/:id/posts/:postId");
  assert.deepEqual(keys, ["id", "postId"]);
  const m = regex.exec("/users/7/posts/99");
  assert.ok(m);
  assert.equal(m![1], "7");
  assert.equal(m![2], "99");
});

test("a param segment does not match across a slash", () => {
  const { regex } = compilePath("/users/:id");
  assert.ok(!regex.test("/users/1/extra"));
});

test("wildcard captures the remainder of the path", () => {
  const { regex, keys } = compilePath("/files/*");
  assert.deepEqual(keys, ["wildcard"]);
  const m = regex.exec("/files/a/b/c.txt");
  assert.ok(m);
  assert.equal(m![1], "a/b/c.txt");
});

test("a trailing slash on the request path still matches", () => {
  const { regex } = compilePath("/users/:id");
  assert.ok(regex.test("/users/1/"));
});

test("regex-special characters in a literal segment are escaped", () => {
  const { regex } = compilePath("/a.b+c");
  assert.ok(regex.test("/a.b+c"));
  assert.ok(!regex.test("/aXb+c"), "the dot must not behave as a wildcard");
});
