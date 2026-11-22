// Seed content for a fresh data directory. Each body is a list of paragraphs.

export const SEED_ARTICLES = [
  {
    slug: 'the-service-worker-lifecycle',
    title: 'The service worker lifecycle',
    summary: 'install, waiting, activate, and why a new worker does not take over straight away.',
    published: '2026-11-18T09:00:00.000Z',
    body: [
      'A service worker is a script that the browser runs apart from the page. It sits between the page and the network. Once it controls a page, every request that page makes goes through its fetch handler.',
      'The first time it runs, the worker gets an install event. That is the moment to download the app shell into a cache. If any file fails to download, installation fails and the browser throws the worker away, so the app never ends up half-cached.',
      'A new version of the worker installs next to the old one and then waits. It only activates when no tab is still using the old version. That rule stops two versions of the app from running against one cache at once. The activate event is where old caches get deleted.',
    ],
  },
  {
    slug: 'choosing-a-caching-strategy',
    title: 'Choosing a caching strategy',
    summary: 'Cache-first for the shell, stale-while-revalidate for articles, and the network for live data.',
    published: '2026-11-19T09:00:00.000Z',
    body: [
      'Cache-first answers from the cache and only goes to the network on a miss. It is right for files that change only when you ship a new version, such as the HTML shell, CSS and JavaScript. The cache name carries the version, so shipping means a new cache.',
      'Stale-while-revalidate answers from the cache straight away and refreshes the cached copy in the background. The reader sees something at once, and the next visit sees the update. It suits articles: a few minutes stale is fine, and a blank page is not.',
      'Some requests should not be cached by the service worker at all. Our article list is JSON that the page stores in IndexedDB itself. Caching it twice would give two copies of the truth that can disagree.',
    ],
  },
  {
    slug: 'instant-loading-with-indexeddb',
    title: 'Instant loading with IndexedDB',
    summary: 'Render what you saved last time, then fetch, then render again.',
    published: '2026-11-20T09:00:00.000Z',
    body: [
      'Network-first data makes the page wait on the slowest part of the system. Instead, the page reads the article list it saved last time from IndexedDB and draws it at once. Then it asks the network, saves the answer, and draws again.',
      'IndexedDB is an asynchronous database built into the browser. It stores structured objects by key and survives restarts. Its API uses events rather than promises, so a small wrapper that turns each request into a promise makes it pleasant to use.',
      'If the network request fails, the page keeps the saved list on screen and says so. The reader can still open any article the service worker has cached.',
    ],
  },
  {
    slug: 'how-web-push-encryption-works',
    title: 'How Web Push encryption works',
    summary: 'ECDH, HKDF and AES-GCM: why the push service cannot read your notifications.',
    published: '2026-11-21T09:00:00.000Z',
    body: [
      'When a page subscribes to push, the browser creates a P-256 key pair and a 16-byte secret. It hands the public key and the secret to the page, which sends them to the server together with an endpoint URL on the browser vendor\'s push service.',
      'To send a message, the server makes a fresh key pair of its own and does ECDH with the browser\'s public key. It mixes in the shared secret with HKDF to get an AES-128-GCM key and a nonce, then encrypts the payload. The push service only ever sees ciphertext.',
      'Separately, the server signs a short-lived JWT with its VAPID key. This shows the push service that the sender is the same server the browser subscribed to. Without it, anyone who learned an endpoint URL could push to it.',
    ],
  },
];
