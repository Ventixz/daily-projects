// A small JSON-file store for articles and push subscriptions.
//
// Writes go to a temporary file first and are then renamed over the real one.
// A crash mid-write leaves either the old file or the new one, never half of
// each. Writes are also queued one at a time, so two concurrent requests
// cannot interleave their read-modify-write cycles.

import fs from 'node:fs/promises';
import path from 'node:path';
import { SEED_ARTICLES } from './articles.js';

export async function openStore(dataDir) {
  await fs.mkdir(dataDir, { recursive: true });
  const file = path.join(dataDir, 'store.json');
  let state;
  try {
    state = JSON.parse(await fs.readFile(file, 'utf8'));
  } catch (err) {
    if (err.code !== 'ENOENT') throw err;
    state = { articles: SEED_ARTICLES.map((a) => ({ ...a })), subscriptions: [] };
  }

  let queue = Promise.resolve();
  function persist() {
    const snapshot = JSON.stringify(state, null, 2);
    const run = queue.then(async () => {
      const tmp = `${file}.${process.pid}.tmp`;
      await fs.writeFile(tmp, snapshot);
      await fs.rename(tmp, file);
    });
    queue = run.catch(() => {});
    return run;
  }
  await persist();

  return {
    listArticles() {
      // Newest first. Return copies so callers cannot mutate the store.
      return state.articles
        .map(({ slug, title, summary, published }) => ({ slug, title, summary, published }))
        .sort((a, b) => b.published.localeCompare(a.published) || a.slug.localeCompare(b.slug));
    },
    getArticle(slug) {
      const a = state.articles.find((x) => x.slug === slug);
      return a ? { ...a } : null;
    },
    async addArticle({ title, summary, body }) {
      const base = slugify(title);
      if (!base) throw new Error('title must contain at least one letter or digit');
      let slug = base;
      for (let n = 2; state.articles.some((a) => a.slug === slug); n++) slug = `${base}-${n}`;
      const article = { slug, title, summary, body, published: new Date().toISOString() };
      state.articles.push(article);
      await persist();
      return { ...article };
    },
    listSubscriptions() {
      return state.subscriptions.map((s) => structuredClone(s));
    },
    async addSubscription(sub) {
      // The endpoint identifies a subscription. Subscribing again replaces
      // the old keys instead of adding a duplicate.
      state.subscriptions = state.subscriptions.filter((s) => s.endpoint !== sub.endpoint);
      state.subscriptions.push(sub);
      await persist();
    },
    async removeSubscription(endpoint) {
      const before = state.subscriptions.length;
      state.subscriptions = state.subscriptions.filter((s) => s.endpoint !== endpoint);
      if (state.subscriptions.length !== before) await persist();
      return state.subscriptions.length !== before;
    },
  };
}

export function slugify(title) {
  return String(title)
    .normalize('NFKD')
    .replace(/[̀-ͯ]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 60)
    .replace(/-+$/, '');
}
