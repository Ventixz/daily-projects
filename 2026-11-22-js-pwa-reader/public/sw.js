// The service worker. The decisions live in sw-strategies.js (unit-tested in
// Node). This file wires them to the lifecycle, fetch, and push events.

importScripts('/sw-strategies.js');
const S = self.SWStrategies;

// ---------------------------------------------------------------- lifecycle

self.addEventListener('install', (event) => {
  // Precache the shell. `cache: 'reload'` skips the browser's HTTP cache, so
  // a new worker cannot precache stale copies of the files it was meant to
  // replace. If any request fails, addAll rejects, the install fails, and
  // the old worker stays in charge.
  event.waitUntil(
    caches.open(S.SHELL_CACHE).then((cache) =>
      cache.addAll(S.SHELL_FILES.map((url) => new Request(url, { cache: 'reload' })))),
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    const keys = await caches.keys();
    await Promise.all(S.cachesToDelete(keys, [S.SHELL_CACHE, S.ARTICLE_CACHE]).map((k) => caches.delete(k)));
    // Take control of pages that loaded before this worker existed. On the
    // first visit, this lets the page work offline without a reload.
    await self.clients.claim();
  })());
});

// A new version waits until the user agrees to switch. The page shows a
// "new version" banner and sends this message when the button is clicked.
self.addEventListener('message', (event) => {
  if (event.data && event.data.type === 'SKIP_WAITING') self.skipWaiting();
});

// ---------------------------------------------------------------- fetch

function offlinePage() {
  return caches.match('/offline.html').then((r) => r || new Response('Offline', { status: 503, headers: { 'Content-Type': 'text/plain' } }));
}

self.addEventListener('fetch', (event) => {
  const request = event.request;
  const route = S.routeFor(request, self.location.origin);

  if (route === 'shell') {
    event.respondWith(caches.open(S.SHELL_CACHE).then((cache) => S.cacheFirst(request, cache, fetch)));
    return;
  }

  if (route === 'article') {
    const work = caches.open(S.ARTICLE_CACHE).then((cache) => S.staleWhileRevalidate(request, cache, fetch));
    // waitUntil must be called synchronously, inside the event dispatch.
    // That is why we pass promises here instead of calling it later.
    event.waitUntil(work.then((w) => w.update));
    event.respondWith(work.then((w) => w.response).catch(offlinePage));
    return;
  }

  if (request.mode === 'navigate') {
    event.respondWith(fetch(request).catch(offlinePage));
  }
  // Anything else (API calls, POSTs, other origins): no respondWith, so the
  // browser handles it as if there were no service worker.
});

// ---------------------------------------------------------------- push

self.addEventListener('push', (event) => {
  // The browser has already decrypted the payload, using the keys it created
  // at subscribe time.
  const data = S.parsePushData(event.data ? event.data.text() : '');
  event.waitUntil((async () => {
    // Tell open pages first, so they can show an in-page toast.
    const windows = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
    for (const client of windows) client.postMessage({ type: 'push', payload: data });
    // We subscribed with userVisibleOnly: true, which promises to show a
    // notification for every push. Chrome enforces this.
    try {
      await self.registration.showNotification(data.title, {
        body: data.body,
        icon: '/icons/icon-192.png',
        tag: data.url, // a second push for the same article replaces the first
        data: { url: data.url },
      });
    } catch (err) {
      // For example, notification permission was revoked after subscribing.
      console.warn('showNotification failed:', err);
    }
  })());
});

self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  const target = new URL(S.parsePushData(JSON.stringify(event.notification.data || {})).url, self.location.origin).href;
  event.waitUntil((async () => {
    const windows = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
    const existing = windows.find((c) => c.url === target);
    if (existing) return existing.focus();
    return self.clients.openWindow(target);
  })());
});

// The push service can rotate or expire a subscription. Subscribe again with
// the same server key, and tell the server about the change.
self.addEventListener('pushsubscriptionchange', (event) => {
  event.waitUntil((async () => {
    const { publicKey } = await (await fetch('/api/push/public-key')).json();
    const sub = await self.registration.pushManager.subscribe({
      userVisibleOnly: true,
      applicationServerKey: S.b64urlToBytes(publicKey),
    });
    const json = { 'Content-Type': 'application/json' };
    if (event.oldSubscription) {
      await fetch('/api/push/subscriptions', { method: 'DELETE', headers: json, body: JSON.stringify({ endpoint: event.oldSubscription.endpoint }) });
    }
    await fetch('/api/push/subscriptions', { method: 'POST', headers: json, body: JSON.stringify(sub) });
  })());
});
