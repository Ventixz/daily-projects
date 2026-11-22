// The HTTP server: static app shell, server-rendered article pages, a JSON
// API, push subscription storage, and publishing (which fans out Web Push).
// It uses only node:http.

import http from 'node:http';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { openStore } from './store.js';
import { sendNotification, fromB64url } from './webpush.js';

const PUBLIC_DIR = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'public');
const MAX_BODY = 16 * 1024;

// A fixed table, not a path join. No URL can reach a file outside this list,
// so path traversal is impossible by construction.
const STATIC = {
  '/': ['index.html', 'text/html; charset=utf-8'],
  '/index.html': ['index.html', 'text/html; charset=utf-8'],
  '/offline.html': ['offline.html', 'text/html; charset=utf-8'],
  '/styles.css': ['styles.css', 'text/css; charset=utf-8'],
  '/app.js': ['app.js', 'text/javascript; charset=utf-8'],
  '/idb.js': ['idb.js', 'text/javascript; charset=utf-8'],
  '/sw.js': ['sw.js', 'text/javascript; charset=utf-8'],
  '/sw-strategies.js': ['sw-strategies.js', 'text/javascript; charset=utf-8'],
  '/manifest.webmanifest': ['manifest.webmanifest', 'application/manifest+json'],
  '/icons/icon-192.png': ['icons/icon-192.png', 'image/png'],
  '/icons/icon-512.png': ['icons/icon-512.png', 'image/png'],
  '/icons/maskable-512.png': ['icons/maskable-512.png', 'image/png'],
};

// Every subscription endpoint is a URL that this server will later POST to.
// If any URL were accepted, anyone could make the server send requests into
// its own private network (SSRF). So endpoints must use HTTPS on a known
// browser push service. Tests pass their own check for a local fake service.
const PUSH_HOSTS = [
  /^fcm\.googleapis\.com$/,
  /^updates\.push\.services\.mozilla\.com$/,
  /^[a-z0-9-]+\.notify\.windows\.com$/,
  /^web\.push\.apple\.com$/,
  /^[a-z0-9-]+\.push\.apple\.com$/,
];
export function isKnownPushEndpoint(endpoint) {
  try {
    const u = new URL(endpoint);
    return u.protocol === 'https:' && !u.port && PUSH_HOSTS.some((re) => re.test(u.hostname));
  } catch {
    return false;
  }
}

export function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
}

export function renderArticlePage(article) {
  const paragraphs = article.body.map((p) => `      <p>${escapeHtml(p)}</p>`).join('\n');
  const date = article.published.slice(0, 10);
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>${escapeHtml(article.title)} · Offline Reader</title>
  <meta name="theme-color" content="#1f4e5f">
  <link rel="manifest" href="/manifest.webmanifest">
  <link rel="icon" href="/icons/icon-192.png">
  <link rel="stylesheet" href="/styles.css">
</head>
<body>
  <header class="bar"><a class="brand" href="/">Offline Reader</a><span id="net-status" class="pill" hidden>Offline</span></header>
  <main>
    <article data-slug="${escapeHtml(article.slug)}">
      <h1>${escapeHtml(article.title)}</h1>
      <p class="meta"><time datetime="${escapeHtml(article.published)}">${escapeHtml(date)}</time></p>
${paragraphs}
    </article>
    <p><a href="/">← All articles</a></p>
  </main>
  <script src="/app.js" defer></script>
</body>
</html>
`;
}

function parseArticleInput(input) {
  if (!input || typeof input !== 'object') return 'body must be a JSON object';
  const { title, summary } = input;
  let { body } = input;
  if (typeof title !== 'string' || !title.trim() || title.length > 200) return 'title must be 1-200 characters';
  if (typeof summary !== 'string' || !summary.trim() || summary.length > 300) return 'summary must be 1-300 characters';
  if (typeof body === 'string') body = body.split(/\n\s*\n/);
  if (!Array.isArray(body) || !body.every((p) => typeof p === 'string')) return 'body must be a string or an array of strings';
  body = body.map((p) => p.trim()).filter(Boolean);
  if (body.length === 0) return 'body must not be empty';
  return { title: title.trim(), summary: summary.trim(), body };
}

export function validateSubscription(input, endpointAllowed = isKnownPushEndpoint) {
  if (!input || typeof input !== 'object') return 'subscription must be an object';
  const { endpoint, keys } = input;
  if (typeof endpoint !== 'string' || endpoint.length > 2048) return 'endpoint must be a URL string';
  if (!endpointAllowed(endpoint)) return 'endpoint is not a recognised push service';
  if (!keys || typeof keys !== 'object') return 'keys are required';
  try {
    const p256dh = fromB64url(keys.p256dh);
    const auth = fromB64url(keys.auth);
    if (p256dh.length !== 65 || p256dh[0] !== 4) return 'keys.p256dh must be an uncompressed P-256 point';
    if (auth.length !== 16) return 'keys.auth must be 16 bytes';
    // Reject keys that are not on the curve now, rather than at send time.
    // computeSecret needs our own key pair, so generate a throwaway one.
    const probe = crypto.createECDH('prime256v1');
    probe.generateKeys();
    probe.computeSecret(p256dh);
  } catch {
    return 'keys are not valid base64url P-256 / auth values';
  }
  return { endpoint, keys: { p256dh: keys.p256dh, auth: keys.auth } };
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    const onData = (c) => {
      size += c.length;
      if (size > MAX_BODY) {
        // Stop buffering and answer 413 at once. The rest of the body is
        // read and thrown away. The error handler sends `Connection: close`
        // so the socket is not reused. Destroying the socket here instead
        // would mean the client never sees the 413.
        req.off('data', onData);
        req.resume();
        reject(Object.assign(new Error('request body too large'), { status: 413 }));
        return;
      }
      chunks.push(c);
    };
    req.on('data', onData);
    req.on('end', () => { if (size <= MAX_BODY) resolve(Buffer.concat(chunks).toString('utf8')); });
    req.on('error', reject);
  });
}

async function readJson(req) {
  const type = req.headers['content-type'] || '';
  if (!/^application\/json\b/i.test(type)) throw Object.assign(new Error('Content-Type must be application/json'), { status: 415 });
  const text = await readBody(req);
  try {
    return JSON.parse(text);
  } catch {
    throw Object.assign(new Error('malformed JSON'), { status: 400 });
  }
}

function tokensEqual(a, b) {
  // Hash both sides first. timingSafeEqual needs equal lengths, and hashing
  // also keeps the comparison from revealing the token's length.
  const ha = crypto.createHash('sha256').update(String(a)).digest();
  const hb = crypto.createHash('sha256').update(String(b)).digest();
  return crypto.timingSafeEqual(ha, hb);
}

/**
 * Create the server. Options:
 *   dataDir          where store.json lives
 *   vapid            { publicKey, privateKey, subject }
 *   adminToken       bearer token required to publish
 *   endpointAllowed  optional override of isKnownPushEndpoint (tests)
 *   fetchImpl        optional fetch for sending pushes (tests)
 */
export async function createServer({ dataDir, vapid, adminToken, endpointAllowed = isKnownPushEndpoint, fetchImpl = fetch }) {
  if (!adminToken) throw new Error('adminToken is required');
  const store = await openStore(dataDir);

  const baseHeaders = {
    'X-Content-Type-Options': 'nosniff',
    'Referrer-Policy': 'same-origin',
    'Content-Security-Policy': "default-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'",
  };

  function send(res, status, body, headers = {}) {
    res.writeHead(status, { ...baseHeaders, ...headers });
    res.end(body);
  }
  function sendJson(res, status, value, headers = {}) {
    send(res, status, JSON.stringify(value), { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...headers });
  }

  async function publish(article) {
    const payload = JSON.stringify({ title: 'New article', body: article.title, url: `/articles/${article.slug}` });
    const subs = store.listSubscriptions();
    const results = await Promise.allSettled(subs.map((s) => sendNotification(s, payload, { vapid, fetchImpl })));
    const summary = { sent: 0, failed: 0, removed: 0 };
    for (const [i, r] of results.entries()) {
      if (r.status === 'fulfilled' && r.value.status >= 200 && r.value.status < 300) summary.sent++;
      else if (r.status === 'fulfilled' && r.value.gone) {
        // The browser unsubscribed, or the user cleared site data. The
        // subscription will never work again, so forget it.
        await store.removeSubscription(subs[i].endpoint);
        summary.removed++;
      } else summary.failed++;
    }
    return summary;
  }

  async function handle(req, res) {
    const url = new URL(req.url, 'http://localhost');
    const p = url.pathname;
    const method = req.method;

    if ((method === 'GET' || method === 'HEAD') && Object.hasOwn(STATIC, p)) {
      const [file, type] = STATIC[p];
      const data = await fs.readFile(path.join(PUBLIC_DIR, file));
      const headers = { 'Content-Type': type, 'Cache-Control': 'no-cache' };
      // The worker script itself must always be revalidated. If the HTTP
      // cache served a stale sw.js, users would never get an update.
      if (p === '/sw.js') headers['Cache-Control'] = 'no-cache, no-store';
      return send(res, 200, method === 'HEAD' ? undefined : data, headers);
    }

    let m;
    if (method === 'GET' && (m = p.match(/^\/articles\/([a-z0-9-]+)$/))) {
      const article = store.getArticle(m[1]);
      if (!article) return send(res, 404, 'Not found', { 'Content-Type': 'text/plain; charset=utf-8' });
      return send(res, 200, renderArticlePage(article), { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-cache' });
    }

    if (p === '/api/articles') {
      if (method === 'GET') return sendJson(res, 200, store.listArticles());
      if (method === 'POST') {
        const auth = req.headers.authorization || '';
        const token = auth.startsWith('Bearer ') ? auth.slice(7) : '';
        if (!token || !tokensEqual(token, adminToken)) {
          return sendJson(res, 401, { error: 'admin token required' }, { 'WWW-Authenticate': 'Bearer' });
        }
        const input = parseArticleInput(await readJson(req));
        if (typeof input === 'string') return sendJson(res, 400, { error: input });
        const article = await store.addArticle(input);
        const push = await publish(article);
        return sendJson(res, 201, { article, push }, { Location: `/articles/${article.slug}` });
      }
      return sendJson(res, 405, { error: 'method not allowed' }, { Allow: 'GET, POST' });
    }

    if (method === 'GET' && (m = p.match(/^\/api\/articles\/([a-z0-9-]+)$/))) {
      const article = store.getArticle(m[1]);
      return article ? sendJson(res, 200, article) : sendJson(res, 404, { error: 'no such article' });
    }

    if (method === 'GET' && p === '/api/push/public-key') {
      return sendJson(res, 200, { publicKey: vapid.publicKey });
    }

    if (p === '/api/push/subscriptions') {
      if (method === 'POST') {
        const sub = validateSubscription(await readJson(req), endpointAllowed);
        if (typeof sub === 'string') return sendJson(res, 400, { error: sub });
        await store.addSubscription(sub);
        return sendJson(res, 201, { ok: true });
      }
      if (method === 'DELETE') {
        const body = await readJson(req);
        if (!body || typeof body.endpoint !== 'string') return sendJson(res, 400, { error: 'endpoint is required' });
        const removed = await store.removeSubscription(body.endpoint);
        return removed ? send(res, 204) : sendJson(res, 404, { error: 'no such subscription' });
      }
      return sendJson(res, 405, { error: 'method not allowed' }, { Allow: 'POST, DELETE' });
    }

    if (p.startsWith('/api/')) return sendJson(res, 404, { error: 'not found' });
    return send(res, 404, 'Not found', { 'Content-Type': 'text/plain; charset=utf-8' });
  }

  const server = http.createServer((req, res) => {
    handle(req, res).catch((err) => {
      const status = err.status || 500;
      if (status === 500) console.error(err);
      if (!res.headersSent) {
        sendJson(res, status, { error: status === 500 ? 'internal error' : err.message }, status === 413 ? { Connection: 'close' } : {});
      }
      else res.destroy();
    });
  });
  server.store = store;
  return server;
}
