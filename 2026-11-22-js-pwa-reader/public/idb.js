// A small promise wrapper around IndexedDB for the article list.
//
// IndexedDB reports results through events (onsuccess, onerror, oncomplete).
// The two helpers below turn a request or a transaction into a promise.
// Everything else is plain async code.

(function (root) {
  'use strict';

  const DB_NAME = 'offline-reader';
  const DB_VERSION = 1;

  function request(req) {
    return new Promise(function (resolve, reject) {
      req.onsuccess = function () { resolve(req.result); };
      req.onerror = function () { reject(req.error); };
    });
  }

  // A write is only durable once the transaction completes. A successful
  // put() request alone does not guarantee that.
  function done(tx) {
    return new Promise(function (resolve, reject) {
      tx.oncomplete = function () { resolve(); };
      tx.onerror = function () { reject(tx.error); };
      tx.onabort = function () { reject(tx.error || new Error('transaction aborted')); };
    });
  }

  let dbPromise = null;
  function open() {
    if (dbPromise) return dbPromise;
    dbPromise = new Promise(function (resolve, reject) {
      const req = indexedDB.open(DB_NAME, DB_VERSION);
      // onupgradeneeded is the only place where the schema can change. It
      // runs when the database is new or DB_VERSION goes up. Later versions
      // would add `if (event.oldVersion < 2) { ... }` steps here.
      req.onupgradeneeded = function () {
        const db = req.result;
        const articles = db.createObjectStore('articles', { keyPath: 'slug' });
        articles.createIndex('published', 'published');
        db.createObjectStore('meta');
      };
      req.onsuccess = function () {
        const db = req.result;
        // If another tab opens a newer version, close our connection so
        // that tab's upgrade is not blocked forever.
        db.onversionchange = function () { db.close(); dbPromise = null; };
        resolve(db);
      };
      req.onerror = function () { dbPromise = null; reject(req.error); };
      req.onblocked = function () { console.warn('IndexedDB upgrade blocked by another open tab'); };
    });
    return dbPromise;
  }

  /** The saved list, newest first, and when it was last synced (ms, or null). */
  async function load() {
    const db = await open();
    const tx = db.transaction(['articles', 'meta'], 'readonly');
    const index = tx.objectStore('articles').index('published');
    const articlesP = request(index.getAll());
    const syncedP = request(tx.objectStore('meta').get('lastSynced'));
    const articles = await articlesP;
    const lastSynced = await syncedP;
    return { articles: articles.reverse(), lastSynced: lastSynced === undefined ? null : lastSynced };
  }

  /**
   * Replace the saved list with `articles`. It is one transaction, so a
   * crash or a quota error leaves the old list whole rather than half
   * replaced. We clear and re-add instead of merging, so articles removed on
   * the server disappear here too.
   */
  async function save(articles, now) {
    const db = await open();
    const tx = db.transaction(['articles', 'meta'], 'readwrite');
    const store = tx.objectStore('articles');
    store.clear();
    for (const a of articles) store.put({ slug: a.slug, title: a.title, summary: a.summary, published: a.published });
    tx.objectStore('meta').put(now === undefined ? Date.now() : now, 'lastSynced');
    await done(tx);
  }

  root.ArticleDB = { load: load, save: save };
})(globalThis);
