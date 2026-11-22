// Shared by the service worker (via importScripts), the page, and the Node
// tests. It is a plain script so that a classic service worker can load it,
// and it attaches one object to globalThis. Nothing in here touches `self`,
// `caches` or `fetch` directly. Each strategy is handed a cache and a fetch
// function, which is what lets the tests run it in Node with fakes.

(function (root) {
  'use strict';

  // Bump SHELL_VERSION whenever a shell file changes. The new worker then
  // precaches into a new cache, and activate deletes the old one. Articles
  // live in a separate, unversioned cache, so a shell update does not throw
  // away everything the reader saved for offline.
  const SHELL_VERSION = 'v1';
  const SHELL_CACHE = 'shell-' + SHELL_VERSION;
  const ARTICLE_CACHE = 'articles';
  const SHELL_FILES = [
    '/',
    '/offline.html',
    '/styles.css',
    '/app.js',
    '/idb.js',
    '/sw-strategies.js',
    '/manifest.webmanifest',
    '/icons/icon-192.png',
  ];

  /**
   * Decide how to handle a request. The result is one of:
   *   'shell'    cache-first, from the precached app shell
   *   'article'  stale-while-revalidate, from the article cache
   *   'network'  leave it to the network. Navigations fall back to
   *              /offline.html when the network fails.
   */
  function routeFor(request, origin) {
    const url = new URL(request.url);
    if (request.method !== 'GET' || url.origin !== origin) return 'network';
    if (SHELL_FILES.indexOf(url.pathname) !== -1) return 'shell';
    if (/^\/articles\/[a-z0-9-]+$/.test(url.pathname)) return 'article';
    // Live data: the page keeps its own copy in IndexedDB, so the worker
    // does not cache /api/ responses.
    return 'network';
  }

  // Only cache complete, successful, same-origin responses. Caching a 404 or
  // a 500 would keep serving the error long after the server recovered.
  function isCacheable(response) {
    return !!response && response.ok && response.status === 200 && response.type !== 'opaque';
  }

  /** Answer from the cache. Only go to the network on a miss. */
  async function cacheFirst(request, cache, fetchFn) {
    // ignoreSearch: '/?utm_source=homescreen' is still the shell.
    const cached = await cache.match(request, { ignoreSearch: true });
    if (cached) return cached;
    const response = await fetchFn(request);
    if (isCacheable(response)) await cache.put(request, response.clone());
    return response;
  }

  /**
   * Answer from the cache if possible. Fetch a fresh copy for next time
   * either way.
   *
   * Returns two promises. `response` is what to hand to respondWith. `update`
   * settles when the background refresh is done, and the worker passes it to
   * waitUntil so the browser does not kill the worker halfway through
   * cache.put. `update` never rejects. `response` rejects only when there is
   * nothing cached and the network failed.
   */
  function staleWhileRevalidate(request, cache, fetchFn) {
    const network = Promise.resolve()
      .then(function () { return fetchFn(request); })
      .then(function (response) {
        if (!isCacheable(response)) return response;
        // Clone now: the body of a Response can only be read once. One copy
        // goes into the cache and the other goes to the page.
        const copy = response.clone();
        return cache.put(request, copy).then(function () { return response; });
      });
    const response = cache.match(request).then(function (cached) { return cached || network; });
    const update = network.then(function () {}, function () {});
    return { response: response, update: update };
  }

  /** Which caches to delete on activate: everything not in `keep`. */
  function cachesToDelete(keys, keep) {
    return keys.filter(function (k) { return keep.indexOf(k) === -1; });
  }

  /**
   * Turn a push payload into notification fields. The payload comes from our
   * server, but it is still treated as untrusted. The click URL must be a
   * same-origin path, so a bad payload cannot send the reader off-site.
   */
  function parsePushData(text) {
    const fallback = { title: 'Offline Reader', body: '', url: '/' };
    if (typeof text !== 'string' || text === '') return fallback;
    let data;
    try {
      data = JSON.parse(text);
    } catch (e) {
      return { title: fallback.title, body: text.slice(0, 200), url: '/' };
    }
    if (!data || typeof data !== 'object') return fallback;
    const str = function (v, d, max) { return typeof v === 'string' && v.trim() ? v.slice(0, max) : d; };
    let url = str(data.url, '/', 500);
    if (url[0] !== '/' || url[1] === '/' || url[1] === '\\') url = '/';
    return { title: str(data.title, fallback.title, 100), body: str(data.body, '', 200), url: url };
  }

  /** applicationServerKey has to be bytes. The server sends base64url. */
  function b64urlToBytes(s) {
    const pad = '='.repeat((4 - (s.length % 4)) % 4);
    const bin = atob((s + pad).replace(/-/g, '+').replace(/_/g, '/'));
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }

  root.SWStrategies = {
    SHELL_VERSION: SHELL_VERSION,
    SHELL_CACHE: SHELL_CACHE,
    ARTICLE_CACHE: ARTICLE_CACHE,
    SHELL_FILES: SHELL_FILES,
    routeFor: routeFor,
    isCacheable: isCacheable,
    cacheFirst: cacheFirst,
    staleWhileRevalidate: staleWhileRevalidate,
    cachesToDelete: cachesToDelete,
    parsePushData: parsePushData,
    b64urlToBytes: b64urlToBytes,
  };
})(globalThis);
