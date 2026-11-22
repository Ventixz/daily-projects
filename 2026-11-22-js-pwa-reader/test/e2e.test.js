// End-to-end test in real Chromium, driven by Playwright:
//   - the service worker installs, precaches the shell, and takes control
//   - the article list is saved to IndexedDB
//   - an opened article is cached and gets a "Saved for offline" badge
//   - a push message reaches the worker and shows a toast in the page
//   - with the server stopped, the home page, the saved list and the opened
//     article still work, and an unopened article shows the offline page
//
// Going offline: Playwright's setOffline() is meant to flip navigator.onLine,
// but it does not reliably cut off requests made by a service worker. In this
// Chromium, onLine even stayed true. So the test also closes the server,
// which makes every request fail as it would offline. The page's offline
// indicator reacts to failed fetches, not only to navigator.onLine.
//
// Needs Playwright with Chromium (`npm i -D playwright && npx playwright install chromium`).
// If Playwright is not installed, the test is skipped.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createServer } from '../server/server.js';
import { generateVapidKeys } from '../server/webpush.js';

let chromium;
try {
  ({ chromium } = createRequire(import.meta.url)('playwright'));
} catch {
  chromium = null;
}

test('offline-first reader in Chromium', { skip: chromium ? false : 'playwright not installed', timeout: 60000 }, async () => {
  const dataDir = await fs.mkdtemp(path.join(os.tmpdir(), 'pwa-reader-e2e-'));
  const server = await createServer({ dataDir, vapid: { ...generateVapidKeys(), subject: 'mailto:e2e@example.org' }, adminToken: 'x' });
  const sockets = new Set();
  server.on('connection', (s) => { sockets.add(s); s.on('close', () => sockets.delete(s)); });
  // 127.0.0.1 counts as a secure context, so service workers are allowed
  // without TLS.
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const origin = `http://127.0.0.1:${server.address().port}`;

  const browser = await chromium.launch();
  try {
    const context = await browser.newContext();
    await context.grantPermissions(['notifications'], { origin });
    const page = await context.newPage();
    const pageErrors = [];
    page.on('pageerror', (e) => pageErrors.push(e));

    // --- first visit: online
    await page.goto(origin + '/');
    await page.waitForSelector('body[data-sync="network"]');
    assert.equal(await page.locator('#articles li').count(), 4);
    // clients.claim() lets the first page come under the worker's control
    // without a reload.
    await page.waitForFunction(() => navigator.serviceWorker.controller !== null);

    const shell = await page.evaluate(async () => (await (await caches.open('shell-v1')).keys()).map((r) => new URL(r.url).pathname).sort());
    assert.deepEqual(shell, [...(await page.evaluate(() => SWStrategies.SHELL_FILES))].sort());

    const saved = await page.evaluate(() => ArticleDB.load());
    assert.equal(saved.articles.length, 4);
    assert.equal(saved.articles[0].slug, 'how-web-push-encryption-works', 'newest first');
    assert.ok(saved.lastSynced > 0);

    // --- open one article; stale-while-revalidate caches it
    await page.click('a[href="/articles/the-service-worker-lifecycle"]');
    await page.waitForSelector('article h1');
    await page.waitForFunction(async () => !!(await (await caches.open('articles')).match('/articles/the-service-worker-lifecycle')));

    await page.goto(origin + '/');
    await page.waitForSelector('body[data-sync="network"]');
    const badge = page.locator('li[data-slug="the-service-worker-lifecycle"] .badge');
    assert.equal(await badge.textContent(), 'Saved for offline');
    assert.equal(await page.locator('li[data-slug="choosing-a-caching-strategy"] .badge').count(), 0);
    assert.equal(await page.isVisible('#push-toggle'), true, 'push toggle offered when PushManager exists');

    // --- push: deliver a message straight to the worker over CDP. This
    // skips the vendor push service, which does not exist in a sandbox. The
    // encryption on that leg is covered by test/webpush.test.js.
    const cdp = await context.newCDPSession(page);
    const registrationId = await new Promise((resolve) => {
      cdp.on('ServiceWorker.workerRegistrationUpdated', ({ registrations }) => {
        const reg = registrations.find((r) => r.scopeURL === origin + '/' && !r.isDeleted);
        if (reg) resolve(reg.registrationId);
      });
      cdp.send('ServiceWorker.enable');
    });
    await cdp.send('ServiceWorker.deliverPushMessage', {
      origin,
      registrationId,
      data: JSON.stringify({ title: 'New article', body: 'Push arrived', url: '/articles/choosing-a-caching-strategy' }),
    });
    await page.waitForSelector('#toast:not([hidden])');
    assert.match(await page.textContent('#toast'), /New article\s+Push arrived/);
    assert.equal(await page.getAttribute('#toast a', 'href'), '/articles/choosing-a-caching-strategy');

    // A malicious click URL is replaced before it reaches the page.
    await cdp.send('ServiceWorker.deliverPushMessage', {
      origin, registrationId, data: JSON.stringify({ title: 'x', body: 'evil', url: 'https://evil.example/' }),
    });
    await page.waitForFunction(() => document.querySelector('#toast a')?.textContent === 'evil');
    assert.equal(await page.getAttribute('#toast a', 'href'), '/');

    // --- go offline
    await context.setOffline(true);
    await new Promise((r) => { server.close(r); for (const s of sockets) s.destroy(); });

    await page.reload();
    await page.waitForSelector('body[data-sync="offline"]');
    assert.equal(await page.locator('#articles li').count(), 4, 'list drawn from IndexedDB');
    assert.match(await page.textContent('#sync-status'), /offline/i);
    assert.equal(await page.isVisible('#net-status'), true);

    // The article opened earlier comes from the article cache.
    await page.goto(origin + '/articles/the-service-worker-lifecycle');
    assert.equal(await page.textContent('article h1'), 'The service worker lifecycle');

    // An article never opened shows the offline page, not a browser error.
    await page.goto(origin + '/articles/choosing-a-caching-strategy');
    assert.equal(await page.textContent('#offline-title'), "This page isn't saved yet");

    // The start URL from the manifest works offline too (ignoreSearch).
    await page.goto(origin + '/?source=pwa');
    await page.waitForSelector('body[data-sync="offline"]');

    assert.deepEqual(pageErrors, []);
  } finally {
    await browser.close();
    server.close();
    await fs.rm(dataDir, { recursive: true, force: true });
  }
});
