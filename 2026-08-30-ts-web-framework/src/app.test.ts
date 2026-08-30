import { test } from "node:test";
import assert from "node:assert/strict";
import type { AddressInfo } from "node:net";
import { app } from "./main";

async function withServer<T>(fn: (base: string) => Promise<T>): Promise<T> {
  const server = app.listen(0);
  await new Promise<void>((resolve) => server.once("listening", resolve));
  const { port } = server.address() as AddressInfo;
  const base = `http://127.0.0.1:${port}`;
  try {
    return await fn(base);
  } finally {
    await new Promise<void>((resolve, reject) =>
      server.close((err) => (err ? reject(err) : resolve())),
    );
  }
}

test("GET / returns a plain-text greeting", async () => {
  await withServer(async (base) => {
    const res = await fetch(`${base}/`);
    assert.equal(res.status, 200);
    assert.match(res.headers.get("content-type") ?? "", /text\/plain/);
    assert.equal(await res.text(), "hello from a 20-line web framework");
  });
});

test("GET /users/:id extracts the param and the query string", async () => {
  await withServer(async (base) => {
    const res = await fetch(`${base}/users/42?verbose=true`);
    assert.equal(res.status, 200);
    assert.deepEqual(await res.json(), { id: "42", query: { verbose: "true" } });
  });
});

test("POST /echo round-trips a JSON body", async () => {
  await withServer(async (base) => {
    const res = await fetch(`${base}/echo`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ hello: "world" }),
    });
    assert.equal(res.status, 200);
    assert.deepEqual(await res.json(), { youSent: { hello: "world" } });
  });
});

test("POST /echo with malformed JSON becomes a 400 via the error-handling layer", async () => {
  await withServer(async (base) => {
    const res = await fetch(`${base}/echo`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: "{not json",
    });
    assert.equal(res.status, 400);
    assert.deepEqual(await res.json(), { error: "invalid JSON body" });
  });
});

test("scoped middleware blocks /admin/* without the header", async () => {
  await withServer(async (base) => {
    const res = await fetch(`${base}/admin/dashboard`);
    assert.equal(res.status, 401);
  });
});

test("scoped middleware admits /admin/* with the header, and does not apply outside its prefix", async () => {
  await withServer(async (base) => {
    const admin = await fetch(`${base}/admin/dashboard`, {
      headers: { "x-api-key": "let-me-in" },
    });
    assert.equal(admin.status, 200);
    assert.deepEqual(await admin.json(), { ok: true, area: "admin" });

    // "/administrator" starts with "/admin" as a string, but the middleware's
    // prefix test requires a "/" boundary, so it must not match here.
    const notAdmin = await fetch(`${base}/administrator`);
    assert.equal(notAdmin.status, 404);
  });
});

test("an unmatched route falls through to the 404 handler", async () => {
  await withServer(async (base) => {
    const res = await fetch(`${base}/does-not-exist`);
    assert.equal(res.status, 404);
    assert.deepEqual(await res.json(), { error: "Cannot GET /does-not-exist" });
  });
});

test("a handler that throws synchronously is caught and routed to error middleware", async () => {
  await withServer(async (base) => {
    const res = await fetch(`${base}/boom`);
    assert.equal(res.status, 500);
    assert.deepEqual(await res.json(), { error: "thrown from a handler, on purpose" });
  });
});

test("global middleware runs on every route, including 404s worth of logging", async () => {
  // Regression check for a real bug hit during development: the logging
  // middleware was registered with app.use(fn) (no path), which must match
  // every request, not just the ones a route eventually matches.
  const seen: string[] = [];
  const originalLog = console.log;
  console.log = (msg: string) => seen.push(msg);
  try {
    await withServer(async (base) => {
      await fetch(`${base}/users/1`);
      await fetch(`${base}/does-not-exist`);
    });
  } finally {
    console.log = originalLog;
  }
  assert.ok(seen.some((line) => line.includes("GET /users/1")));
  assert.ok(seen.some((line) => line.includes("GET /does-not-exist")));
});
