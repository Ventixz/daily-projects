// Page script, used on the home page and on article pages.
//   1. registers the service worker and offers updates when one is waiting
//   2. home page only: shows the list saved in IndexedDB, then refreshes it
//   3. home page only: the push notification toggle
//   4. online/offline indicator and in-page toasts for pushes

(function () {
  'use strict';

  const S = globalThis.SWStrategies; // only loaded on the home page
  const DB = globalThis.ArticleDB; // only loaded on the home page
  const $ = (id) => document.getElementById(id);

  // ------------------------------------------------------- service worker

  let swRegistration = null;
  let userAskedForUpdate = false;

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js').then((reg) => {
      swRegistration = reg;
      watchForUpdates(reg);
      initPush(reg);
    }).catch((err) => console.warn('service worker registration failed:', err));

    navigator.serviceWorker.addEventListener('message', (event) => {
      if (event.data && event.data.type === 'push') showToast(event.data.payload);
    });

    // controllerchange also fires on the first visit, when the new worker
    // calls clients.claim(). Only reload if the user clicked "Update".
    navigator.serviceWorker.addEventListener('controllerchange', () => {
      if (userAskedForUpdate) location.reload();
    });
  }

  function watchForUpdates(reg) {
    // With no controller, this is the first install, not an update.
    if (reg.waiting && navigator.serviceWorker.controller) offerUpdate(reg.waiting);
    reg.addEventListener('updatefound', () => {
      const worker = reg.installing;
      if (!worker) return;
      worker.addEventListener('statechange', () => {
        if (worker.state === 'installed' && navigator.serviceWorker.controller) offerUpdate(worker);
      });
    });
  }

  function offerUpdate(worker) {
    const banner = $('update-banner');
    if (!banner) return;
    banner.hidden = false;
    $('update-button').onclick = () => {
      userAskedForUpdate = true;
      worker.postMessage({ type: 'SKIP_WAITING' });
    };
  }

  // ------------------------------------------------------- network status

  // navigator.onLine === false reliably means offline. But true only means
  // "some network interface is up". It stays true behind a captive portal
  // or when our server is down. So a failed fetch also counts as offline,
  // until a later fetch succeeds.
  let lastFetchFailed = false;
  function updateNetStatus() {
    const pill = $('net-status');
    if (pill) pill.hidden = navigator.onLine && !lastFetchFailed;
  }
  window.addEventListener('online', updateNetStatus);
  window.addEventListener('offline', updateNetStatus);
  updateNetStatus();

  // ------------------------------------------------------- article list

  const list = $('articles');
  if (list && DB) loadArticles();

  function setSync(state, message) {
    document.body.dataset.sync = state; // the end-to-end tests read this
    $('sync-status').textContent = message;
  }

  function timeAgo(ms) {
    const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
    if (s < 60) return 'just now';
    if (s < 3600) return Math.round(s / 60) + ' min ago';
    if (s < 86400) return Math.round(s / 3600) + ' h ago';
    return Math.round(s / 86400) + ' days ago';
  }

  async function loadArticles() {
    // Step 1: draw the saved copy at once. There is no network wait.
    let saved = null;
    try {
      saved = await DB.load();
      if (saved.articles.length) {
        await render(saved.articles);
        setSync('saved', 'Showing your saved copy. Checking for new articles…');
      }
    } catch (err) {
      // IndexedDB can be missing, for example in some private modes. The
      // network path below still works.
      console.warn('IndexedDB unavailable:', err);
    }

    // Step 2: ask the network, then draw again and save.
    try {
      const res = await fetch('/api/articles', { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const articles = await res.json();
      await render(articles);
      lastFetchFailed = false;
      updateNetStatus();
      setSync('network', 'Up to date.');
      try { await DB.save(articles); } catch (err) { console.warn('could not save articles:', err); }
    } catch (err) {
      lastFetchFailed = true;
      updateNetStatus();
      if (saved && saved.articles.length) {
        setSync('offline', 'You are offline. Showing the copy saved ' + timeAgo(saved.lastSynced) + '.');
      } else {
        setSync('empty', 'You are offline and no articles have been saved yet.');
      }
    }
  }

  async function isAvailableOffline(slug) {
    if (!('caches' in window) || !S) return false;
    try {
      const cache = await caches.open(S.ARTICLE_CACHE);
      return !!(await cache.match('/articles/' + slug));
    } catch (e) {
      return false;
    }
  }

  async function render(articles) {
    // Build nodes with textContent, never innerHTML. Article text is data,
    // not markup.
    const offline = await Promise.all(articles.map((a) => isAvailableOffline(a.slug)));
    const items = articles.map((a, i) => {
      const li = document.createElement('li');
      li.className = 'card';
      li.dataset.slug = a.slug;
      const link = document.createElement('a');
      link.href = '/articles/' + encodeURIComponent(a.slug);
      link.textContent = a.title;
      const h2 = document.createElement('h2');
      h2.append(link);
      const summary = document.createElement('p');
      summary.textContent = a.summary;
      const meta = document.createElement('p');
      meta.className = 'meta';
      const time = document.createElement('time');
      time.dateTime = a.published;
      time.textContent = a.published.slice(0, 10);
      meta.append(time);
      if (offline[i]) {
        const badge = document.createElement('span');
        badge.className = 'badge';
        badge.textContent = 'Saved for offline';
        meta.append(' ', badge);
      }
      li.append(h2, summary, meta);
      return li;
    });
    list.replaceChildren(...items);
  }

  // ------------------------------------------------------- push

  async function initPush(reg) {
    const button = $('push-toggle');
    if (!button) return;
    if (!('PushManager' in window) || !('Notification' in window) || !S) {
      button.hidden = true;
      return;
    }
    button.hidden = false;
    const refresh = async () => {
      const sub = await reg.pushManager.getSubscription();
      button.textContent = sub ? 'Turn off notifications' : 'Notify me of new articles';
      button.setAttribute('aria-pressed', sub ? 'true' : 'false');
    };
    await refresh();

    button.onclick = async () => {
      button.disabled = true;
      setPushStatus('');
      try {
        const existing = await reg.pushManager.getSubscription();
        if (existing) {
          await fetch('/api/push/subscriptions', {
            method: 'DELETE',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ endpoint: existing.endpoint }),
          });
          await existing.unsubscribe();
        } else {
          // Only ask for permission after a click. Browsers punish sites
          // that ask on page load.
          const permission = await Notification.requestPermission();
          if (permission !== 'granted') throw new Error('notifications are blocked for this site');
          const { publicKey } = await (await fetch('/api/push/public-key')).json();
          const sub = await reg.pushManager.subscribe({
            userVisibleOnly: true,
            applicationServerKey: S.b64urlToBytes(publicKey),
          });
          const res = await fetch('/api/push/subscriptions', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(sub), // PushSubscription.toJSON(): endpoint + keys
          });
          if (!res.ok) {
            await sub.unsubscribe();
            throw new Error((await res.json()).error || 'server rejected the subscription');
          }
        }
      } catch (err) {
        setPushStatus('Could not change notifications: ' + err.message);
      } finally {
        button.disabled = false;
        await refresh();
      }
    };
  }

  function setPushStatus(text) {
    const el = $('push-status');
    if (el) el.textContent = text;
  }

  function showToast(payload) {
    const toast = $('toast');
    if (!toast) return;
    const link = document.createElement('a');
    link.href = payload.url;
    link.textContent = payload.body || payload.title;
    const strong = document.createElement('strong');
    strong.textContent = payload.title;
    toast.replaceChildren(strong, ' ', link);
    toast.hidden = false;
    clearTimeout(showToast.timer);
    showToast.timer = setTimeout(() => { toast.hidden = true; }, 8000);
  }
})();
