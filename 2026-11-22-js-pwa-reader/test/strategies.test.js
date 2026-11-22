// Unit tests for public/sw-strategies.js. They run in Node with a fake Cache
// and a fake fetch. Node 20+ has the real Request and Response classes.

import { test } from 'node:test';
import assert from 'node:assert/strict';

await import('../public/sw-strategies.js');
const S = globalThis.SWStrategies;
const ORIGIN = 'https://reader.example';

class FakeCache {
  constructor() { this.map = new Map(); this.puts = 0; }
  key(req, ignoreSearch) {
    const u = new URL(typeof req === 'string' ? req : req.url, ORIGIN);
    return ignoreSearch ? u.origin + u.pathname : u.href;
  }
  async match(req, opts = {}) {
    if (opts.ignoreSearch) {
      const want = this.key(req, true);
      for (const [k, v] of this.map) if (this.key(k, true) === want) return v.clone();
      return undefined;
    }
    const r = this.map.get(this.key(req, false));
    return r ? r.clone() : undefined;
  }
  async put(req, res) { this.puts++; this.map.set(this.key(req, false), res); }
}

const req = (path, init) => new Request(ORIGIN + path, init);
const ok = (text) => new Response(text, { status: 200 });

function fetchReturning(...responses) {
  const calls = [];
  const fn = async (r) => {
    calls.push(r.url);
    const next = responses.shift();
    if (next instanceof Error) throw next;
    return next;
  };
  fn.calls = calls;
  return fn;
}

test('routeFor: shell files, articles, API, other origins, non-GET', () => {
  assert.equal(S.routeFor(req('/'), ORIGIN), 'shell');
  assert.equal(S.routeFor(req('/styles.css'), ORIGIN), 'shell');
  assert.equal(S.routeFor(req('/sw-strategies.js'), ORIGIN), 'shell');
  assert.equal(S.routeFor(req('/articles/how-web-push-encryption-works'), ORIGIN), 'article');
  assert.equal(S.routeFor(req('/articles/Bad_Slug'), ORIGIN), 'network');
  assert.equal(S.routeFor(req('/articles/a/b'), ORIGIN), 'network');
  assert.equal(S.routeFor(req('/api/articles'), ORIGIN), 'network');
  assert.equal(S.routeFor(req('/sw.js'), ORIGIN), 'network', 'the worker script must never come from its own cache');
  assert.equal(S.routeFor(new Request('https://cdn.example/styles.css'), ORIGIN), 'network');
  assert.equal(S.routeFor(req('/api/push/subscriptions', { method: 'POST', body: '{}' }), ORIGIN), 'network');
});

test('every shell file is routed to the shell cache', () => {
  for (const f of S.SHELL_FILES) assert.equal(S.routeFor(req(f), ORIGIN), 'shell', f);
});

test('cacheFirst: a hit never touches the network', async () => {
  const cache = new FakeCache();
  await cache.put(req('/styles.css'), ok('cached'));
  const fetchFn = fetchReturning(new Error('should not be called'));
  const res = await S.cacheFirst(req('/styles.css'), cache, fetchFn);
  assert.equal(await res.text(), 'cached');
  assert.equal(fetchFn.calls.length, 0);
});

test('cacheFirst: ignores the query string, so /?source=pwa is the shell', async () => {
  const cache = new FakeCache();
  await cache.put(req('/'), ok('shell'));
  const res = await S.cacheFirst(req('/?source=pwa'), cache, fetchReturning());
  assert.equal(await res.text(), 'shell');
});

test('cacheFirst: a miss fetches, caches, and returns a readable body', async () => {
  const cache = new FakeCache();
  const res = await S.cacheFirst(req('/app.js'), cache, fetchReturning(ok('fresh')));
  assert.equal(await res.text(), 'fresh', 'the response returned to the page is still readable');
  assert.equal(await (await cache.match(req('/app.js'))).text(), 'fresh');
});

test('cacheFirst: error responses are passed through but not cached', async () => {
  const cache = new FakeCache();
  const res = await S.cacheFirst(req('/app.js'), cache, fetchReturning(new Response('boom', { status: 500 })));
  assert.equal(res.status, 500);
  assert.equal(cache.puts, 0);
});

test('staleWhileRevalidate: serves the cached copy, then refreshes it', async () => {
  const cache = new FakeCache();
  await cache.put(req('/articles/a'), ok('old'));
  const { response, update } = S.staleWhileRevalidate(req('/articles/a'), cache, fetchReturning(ok('new')));
  assert.equal(await (await response).text(), 'old');
  await update;
  assert.equal(await (await cache.match(req('/articles/a'))).text(), 'new');
});

test('staleWhileRevalidate: with nothing cached, waits for the network', async () => {
  const cache = new FakeCache();
  const { response, update } = S.staleWhileRevalidate(req('/articles/a'), cache, fetchReturning(ok('net')));
  assert.equal(await (await response).text(), 'net');
  await update;
  assert.equal(cache.puts, 1);
});

test('staleWhileRevalidate: offline with a cached copy still answers, and update does not reject', async () => {
  const cache = new FakeCache();
  await cache.put(req('/articles/a'), ok('old'));
  const { response, update } = S.staleWhileRevalidate(req('/articles/a'), cache, fetchReturning(new TypeError('offline')));
  assert.equal(await (await response).text(), 'old');
  await assert.doesNotReject(update);
});

test('staleWhileRevalidate: offline with nothing cached rejects (the worker shows the offline page)', async () => {
  const cache = new FakeCache();
  const { response, update } = S.staleWhileRevalidate(req('/articles/a'), cache, fetchReturning(new TypeError('offline')));
  await assert.rejects(response, TypeError);
  await assert.doesNotReject(update);
});

test('staleWhileRevalidate: a 404 does not overwrite a good cached copy', async () => {
  const cache = new FakeCache();
  await cache.put(req('/articles/a'), ok('good'));
  const { update } = S.staleWhileRevalidate(req('/articles/a'), cache, fetchReturning(new Response('nope', { status: 404 })));
  await update;
  assert.equal(await (await cache.match(req('/articles/a'))).text(), 'good');
});

test('staleWhileRevalidate: a fetch function that throws synchronously is handled', async () => {
  const cache = new FakeCache();
  const { response, update } = S.staleWhileRevalidate(req('/articles/a'), cache, () => { throw new Error('sync'); });
  await assert.rejects(response, /sync/);
  await assert.doesNotReject(update);
});

test('cachesToDelete keeps the current shell and the article cache', () => {
  assert.deepEqual(
    S.cachesToDelete(['shell-v0', 'shell-v1', 'articles', 'something-old'], [S.SHELL_CACHE, S.ARTICLE_CACHE]),
    ['shell-v0', 'something-old'],
  );
  assert.equal(S.SHELL_CACHE, 'shell-' + S.SHELL_VERSION);
});

test('parsePushData: well-formed JSON', () => {
  assert.deepEqual(S.parsePushData(JSON.stringify({ title: 'New article', body: 'Hello', url: '/articles/x' })),
    { title: 'New article', body: 'Hello', url: '/articles/x' });
});

test('parsePushData: empty, plain text, and non-object payloads get defaults', () => {
  assert.deepEqual(S.parsePushData(''), { title: 'Offline Reader', body: '', url: '/' });
  assert.deepEqual(S.parsePushData('just text'), { title: 'Offline Reader', body: 'just text', url: '/' });
  assert.deepEqual(S.parsePushData('42'), { title: 'Offline Reader', body: '', url: '/' });
  assert.deepEqual(S.parsePushData('null'), { title: 'Offline Reader', body: '', url: '/' });
});

test('parsePushData: click URLs must be same-origin paths', () => {
  for (const bad of ['https://evil.example/', '//evil.example/x', '/\\evil.example', 'javascript:alert(1)', 'articles/x']) {
    assert.equal(S.parsePushData(JSON.stringify({ url: bad })).url, '/', bad);
  }
  assert.equal(S.parsePushData(JSON.stringify({ url: 42, title: ['x'] })).title, 'Offline Reader');
});

test('parsePushData: long fields are truncated', () => {
  const p = S.parsePushData(JSON.stringify({ title: 'x'.repeat(500), body: 'y'.repeat(500) }));
  assert.equal(p.title.length, 100);
  assert.equal(p.body.length, 200);
});

test('b64urlToBytes decodes unpadded base64url', () => {
  assert.deepEqual([...S.b64urlToBytes('-_8')], [0xfb, 0xff]);
  assert.deepEqual([...S.b64urlToBytes('AQID')], [1, 2, 3]);
  assert.equal(S.b64urlToBytes(Buffer.alloc(65, 4).toString('base64url')).length, 65);
});
