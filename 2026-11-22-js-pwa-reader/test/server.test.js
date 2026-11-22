// HTTP-level tests for server/server.js. Publishing is tested end to end
// against a local fake push service, which checks VAPID and decrypts each
// message the way a browser would.

import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createServer, isKnownPushEndpoint, validateSubscription, escapeHtml } from '../server/server.js';
import { generateVapidKeys, deriveKeys, b64url, fromB64url } from '../server/webpush.js';
import { slugify } from '../server/store.js';

const ADMIN = 'test-admin-token';
let dataDir, server, base, pushService, pushBase;
const vapid = { ...generateVapidKeys(), subject: 'mailto:test@example.org' };

// ---- the fake push service ------------------------------------------------
// It decrypts with the private key registered for each endpoint and records
// what it got. Endpoints under /gone/ answer 410, as a real push service does
// after the user unsubscribes.
const browsers = new Map(); // endpoint path -> { ecdh, auth }
const delivered = []; // { path, plaintext, ttl, vapidOk }

function decrypt(body, { ecdh, auth }) {
  const salt = body.subarray(0, 16);
  const idlen = body[20];
  const asPublic = body.subarray(21, 21 + idlen);
  const record = body.subarray(21 + idlen);
  const { cek, nonce } = deriveKeys({ ecdhSecret: ecdh.computeSecret(asPublic), authSecret: auth, uaPublic: ecdh.getPublicKey(), asPublic, salt });
  const d = crypto.createDecipheriv('aes-128-gcm', cek, nonce);
  d.setAuthTag(record.subarray(-16));
  const padded = Buffer.concat([d.update(record.subarray(0, -16)), d.final()]);
  return padded.subarray(0, padded.lastIndexOf(2)).toString('utf8');
}

function vapidValid(header) {
  const m = /^vapid t=([^.]+)\.([^.]+)\.([^,]+), k=(.+)$/.exec(header || '');
  if (!m || m[4] !== vapid.publicKey) return false;
  const pub = fromB64url(m[4]);
  const key = crypto.createPublicKey({ key: { kty: 'EC', crv: 'P-256', x: b64url(pub.subarray(1, 33)), y: b64url(pub.subarray(33)) }, format: 'jwk' });
  return crypto.verify('sha256', Buffer.from(`${m[1]}.${m[2]}`), { key, dsaEncoding: 'ieee-p1363' }, fromB64url(m[3]));
}

function newBrowser(kind = 'ok') {
  const ecdh = crypto.createECDH('prime256v1');
  ecdh.generateKeys();
  const auth = crypto.randomBytes(16);
  const p = `/${kind}/${crypto.randomUUID()}`;
  browsers.set(p, { ecdh, auth });
  return { endpoint: pushBase + p, keys: { p256dh: b64url(ecdh.getPublicKey()), auth: b64url(auth) } };
}

// ---- helpers ---------------------------------------------------------------
async function req(method, p, { body, headers = {} } = {}) {
  const init = { method, headers: { ...headers } };
  if (body !== undefined) {
    init.body = typeof body === 'string' ? body : JSON.stringify(body);
    init.headers['Content-Type'] ??= 'application/json';
  }
  const res = await fetch(base + p, init);
  const text = await res.text();
  let json;
  try { json = JSON.parse(text); } catch { /* not JSON */ }
  return { status: res.status, headers: res.headers, text, json };
}
const admin = { Authorization: `Bearer ${ADMIN}` };
const listen = (s) => new Promise((r) => s.listen(0, '127.0.0.1', () => r(`http://127.0.0.1:${s.address().port}`)));

before(async () => {
  pushService = http.createServer((rq, rs) => {
    const chunks = [];
    rq.on('data', (c) => chunks.push(c));
    rq.on('end', () => {
      if (rq.url.startsWith('/gone/')) { rs.writeHead(410); return rs.end(); }
      if (rq.url.startsWith('/broken/')) { rs.writeHead(500); return rs.end(); }
      const b = browsers.get(rq.url);
      if (!b || rq.headers['content-encoding'] !== 'aes128gcm') { rs.writeHead(400); return rs.end(); }
      delivered.push({ path: rq.url, plaintext: decrypt(Buffer.concat(chunks), b), ttl: rq.headers.ttl, vapidOk: vapidValid(rq.headers.authorization) });
      rs.writeHead(201);
      rs.end();
    });
  });
  pushBase = await listen(pushService);

  dataDir = await fs.mkdtemp(path.join(os.tmpdir(), 'pwa-reader-'));
  server = await createServer({
    dataDir, vapid, adminToken: ADMIN,
    endpointAllowed: (e) => e.startsWith(pushBase + '/'),
  });
  base = await listen(server);
});

after(async () => {
  server.close();
  pushService.close();
  await fs.rm(dataDir, { recursive: true, force: true });
});

// ---- static shell ------------------------------------------------------------
test('serves the shell with the right types and security headers', async () => {
  const home = await req('GET', '/');
  assert.equal(home.status, 200);
  assert.match(home.headers.get('content-type'), /^text\/html/);
  assert.match(home.text, /<link rel="manifest" href="\/manifest.webmanifest">/);
  assert.match(home.headers.get('content-security-policy'), /default-src 'self'/);
  assert.equal(home.headers.get('x-content-type-options'), 'nosniff');

  const manifest = await req('GET', '/manifest.webmanifest');
  assert.equal(manifest.headers.get('content-type'), 'application/manifest+json');
  assert.equal(manifest.json.display, 'standalone');
  assert.ok(manifest.json.icons.some((i) => i.sizes === '512x512' && i.purpose === 'maskable'));

  const icon = await fetch(base + '/icons/icon-192.png');
  assert.equal(icon.headers.get('content-type'), 'image/png');
  assert.deepEqual([...new Uint8Array(await icon.arrayBuffer()).slice(0, 4)], [0x89, 0x50, 0x4e, 0x47]);
});

test('sw.js is never cached by HTTP', async () => {
  const sw = await req('GET', '/sw.js');
  assert.equal(sw.status, 200);
  assert.match(sw.headers.get('content-type'), /javascript/);
  assert.match(sw.headers.get('cache-control'), /no-store/);
  assert.match(sw.text, /importScripts\('\/sw-strategies.js'\)/);
});

test('every precached shell file exists on the server', async () => {
  await import('../public/sw-strategies.js');
  for (const f of globalThis.SWStrategies.SHELL_FILES) {
    assert.equal((await req('GET', f)).status, 200, f);
  }
});

test('unknown paths and traversal attempts are 404', async () => {
  for (const p of ['/nope', '/../server/server.js', '/%2e%2e/package.json', '/icons/../../package.json', '/data/store.json']) {
    assert.equal((await req('GET', p)).status, 404, p);
  }
  assert.equal((await req('GET', '/api/nope')).json.error, 'not found');
});

test('HEAD returns headers without a body', async () => {
  const res = await fetch(base + '/styles.css', { method: 'HEAD' });
  assert.equal(res.status, 200);
  assert.equal(await res.text(), '');
});

// ---- articles ------------------------------------------------------------------
test('article list is newest first and has no bodies', async () => {
  const { status, json } = await req('GET', '/api/articles');
  assert.equal(status, 200);
  assert.ok(json.length >= 4);
  const dates = json.map((a) => a.published);
  assert.deepEqual(dates, [...dates].sort().reverse());
  assert.ok(json.every((a) => !('body' in a)));
});

test('article page is server-rendered HTML; missing slugs are 404', async () => {
  const page = await req('GET', '/articles/the-service-worker-lifecycle');
  assert.equal(page.status, 200);
  assert.match(page.text, /<h1>The service worker lifecycle<\/h1>/);
  assert.match(page.text, /<script src="\/app.js" defer><\/script>/);
  assert.equal((await req('GET', '/articles/does-not-exist')).status, 404);
  assert.equal((await req('GET', '/api/articles/does-not-exist')).status, 404);
  assert.equal((await req('GET', '/api/articles/the-service-worker-lifecycle')).json.body.length, 3);
});

test('publishing requires the admin token', async () => {
  const body = { title: 'x', summary: 'y', body: 'z' };
  assert.equal((await req('POST', '/api/articles', { body })).status, 401);
  const wrong = await req('POST', '/api/articles', { body, headers: { Authorization: 'Bearer nope' } });
  assert.equal(wrong.status, 401);
  assert.equal(wrong.headers.get('www-authenticate'), 'Bearer');
});

test('publishing validates input', async () => {
  const cases = [
    [{ summary: 's', body: 'b' }, /title/],
    [{ title: 't', body: 'b' }, /summary/],
    [{ title: 't', summary: 's', body: 5 }, /body/],
    [{ title: 't', summary: 's', body: '   ' }, /empty/],
    [{ title: 'x'.repeat(201), summary: 's', body: 'b' }, /title/],
  ];
  for (const [body, re] of cases) {
    const r = await req('POST', '/api/articles', { body, headers: admin });
    assert.equal(r.status, 400, JSON.stringify(body));
    assert.match(r.json.error, re);
  }
  assert.equal((await req('POST', '/api/articles', { body: '{bad', headers: admin })).status, 400);
  assert.equal((await req('POST', '/api/articles', { body: 'a=b', headers: { ...admin, 'Content-Type': 'application/x-www-form-urlencoded' } })).status, 415);
  assert.equal((await req('POST', '/api/articles', { body: { title: 't', summary: 's', body: 'x'.repeat(20000) }, headers: admin })).status, 413);
});

test('article HTML is escaped', async () => {
  const r = await req('POST', '/api/articles', {
    headers: admin,
    body: { title: '<script>alert(1)</script> & "quotes"', summary: 's', body: 'Para <b>one</b>\n\nPara two' },
  });
  assert.equal(r.status, 201);
  assert.equal(r.json.article.slug, 'script-alert-1-script-quotes');
  assert.equal(r.headers.get('location'), `/articles/${r.json.article.slug}`);
  const page = await req('GET', `/articles/${r.json.article.slug}`);
  assert.ok(!page.text.includes('<script>alert(1)</script>'));
  assert.match(page.text, /&lt;script&gt;alert\(1\)&lt;\/script&gt; &amp; &quot;quotes&quot;/);
  assert.match(page.text, /<p>Para &lt;b&gt;one&lt;\/b&gt;<\/p>\s*<p>Para two<\/p>/);
});

test('duplicate titles get distinct slugs', async () => {
  const body = { title: 'Same Title', summary: 's', body: 'b' };
  const a = await req('POST', '/api/articles', { body, headers: admin });
  const b = await req('POST', '/api/articles', { body, headers: admin });
  assert.equal(a.json.article.slug, 'same-title');
  assert.equal(b.json.article.slug, 'same-title-2');
});

// ---- push subscriptions ------------------------------------------------------------
test('public key endpoint returns the VAPID public key', async () => {
  const r = await req('GET', '/api/push/public-key');
  assert.equal(r.json.publicKey, vapid.publicKey);
  assert.equal(fromB64url(r.json.publicKey).length, 65);
});

test('subscriptions are validated', async () => {
  const good = newBrowser();
  const offCurve = Buffer.alloc(65, 1);
  offCurve[0] = 4;
  const cases = [
    [{ ...good, endpoint: 'http://169.254.169.254/latest/meta-data' }, /push service/],
    [{ endpoint: good.endpoint }, /keys/],
    [{ ...good, keys: { ...good.keys, auth: b64url(Buffer.alloc(4)) } }, /auth/],
    [{ ...good, keys: { ...good.keys, p256dh: b64url(offCurve) } }, /valid/],
    [{ ...good, keys: { ...good.keys, p256dh: '***' } }, /valid/],
  ];
  for (const [body, re] of cases) {
    const r = await req('POST', '/api/push/subscriptions', { body });
    assert.equal(r.status, 400);
    assert.match(r.json.error, re);
  }
});

test('the default endpoint allowlist only accepts HTTPS on known push services', () => {
  assert.equal(isKnownPushEndpoint('https://fcm.googleapis.com/fcm/send/abc'), true);
  assert.equal(isKnownPushEndpoint('https://updates.push.services.mozilla.com/wpush/v2/abc'), true);
  assert.equal(isKnownPushEndpoint('https://wns2-by3p.notify.windows.com/w/?token=abc'), true);
  assert.equal(isKnownPushEndpoint('https://web.push.apple.com/abc'), true);
  assert.equal(isKnownPushEndpoint('http://fcm.googleapis.com/fcm/send/abc'), false);
  assert.equal(isKnownPushEndpoint('https://fcm.googleapis.com:8443/x'), false);
  assert.equal(isKnownPushEndpoint('https://fcm.googleapis.com.evil.example/x'), false);
  assert.equal(isKnownPushEndpoint('https://localhost/x'), false);
  assert.equal(isKnownPushEndpoint('not a url'), false);
  assert.equal(typeof validateSubscription(null), 'string');
});

test('publish pushes an encrypted, VAPID-signed message to every subscriber and prunes 410s', async () => {
  const alice = newBrowser();
  const bob = newBrowser();
  const gone = newBrowser('gone');
  const broken = newBrowser('broken');
  for (const s of [alice, bob, gone, broken]) {
    assert.equal((await req('POST', '/api/push/subscriptions', { body: s })).status, 201);
  }
  // Subscribing again with the same endpoint replaces the old entry rather
  // than adding a duplicate.
  assert.equal((await req('POST', '/api/push/subscriptions', { body: alice })).status, 201);
  assert.equal(server.store.listSubscriptions().filter((s) => s.endpoint === alice.endpoint).length, 1);

  delivered.length = 0;
  const r = await req('POST', '/api/articles', {
    headers: admin,
    body: { title: 'Push day', summary: 'It works', body: ['One.', 'Two.'] },
  });
  assert.equal(r.status, 201);
  assert.deepEqual(r.json.push, { sent: 2, failed: 1, removed: 1 });

  assert.equal(delivered.length, 2);
  for (const d of delivered) {
    assert.equal(d.vapidOk, true);
    assert.equal(d.ttl, '86400');
    assert.deepEqual(JSON.parse(d.plaintext), { title: 'New article', body: 'Push day', url: '/articles/push-day' });
  }
  assert.deepEqual(new Set(delivered.map((d) => d.path)), new Set([alice, bob].map((s) => new URL(s.endpoint).pathname)));

  const left = server.store.listSubscriptions().map((s) => s.endpoint);
  assert.ok(!left.includes(gone.endpoint), '410 subscription was removed');
  assert.ok(left.includes(broken.endpoint), 'a 500 is temporary, so the subscription is kept');
});

test('unsubscribe removes the subscription; unknown endpoints are 404', async () => {
  const s = newBrowser();
  await req('POST', '/api/push/subscriptions', { body: s });
  assert.equal((await req('DELETE', '/api/push/subscriptions', { body: { endpoint: s.endpoint } })).status, 204);
  assert.equal((await req('DELETE', '/api/push/subscriptions', { body: { endpoint: s.endpoint } })).status, 404);
  assert.equal((await req('DELETE', '/api/push/subscriptions', { body: {} })).status, 400);
  assert.equal((await req('PUT', '/api/push/subscriptions', { body: {} })).status, 405);
});

// ---- persistence ---------------------------------------------------------------
test('state survives a restart: a second server on the same data dir sees it', async () => {
  const s = newBrowser();
  await req('POST', '/api/push/subscriptions', { body: s });
  await req('POST', '/api/articles', { headers: admin, body: { title: 'Persisted', summary: 's', body: 'b' } });
  const again = await createServer({ dataDir, vapid, adminToken: ADMIN, endpointAllowed: () => true });
  assert.ok(again.store.getArticle('persisted'));
  assert.ok(again.store.listSubscriptions().some((x) => x.endpoint === s.endpoint));
  const files = await fs.readdir(dataDir);
  assert.deepEqual(files.filter((f) => f.endsWith('.tmp')), [], 'no temp files left behind');
});

test('concurrent publishes are all persisted', async () => {
  await Promise.all(Array.from({ length: 10 }, (_, i) =>
    req('POST', '/api/articles', { headers: admin, body: { title: `Burst ${i}`, summary: 's', body: 'b' } })));
  const onDisk = JSON.parse(await fs.readFile(path.join(dataDir, 'store.json'), 'utf8'));
  for (let i = 0; i < 10; i++) assert.ok(onDisk.articles.some((a) => a.slug === `burst-${i}`), `burst-${i}`);
});

test('helpers: slugify and escapeHtml', () => {
  assert.equal(slugify('  Héllo, Wörld!  '), 'hello-world');
  assert.equal(slugify('!!!'), '');
  assert.equal(slugify('a'.repeat(100)).length, 60);
  assert.equal(escapeHtml(`<a href="x">'&'</a>`), '&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;');
});
