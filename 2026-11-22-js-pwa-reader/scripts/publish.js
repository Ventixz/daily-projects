// Publish an article and push a notification to every subscriber.
//
//   ADMIN_TOKEN=... npm run publish-article -- "Title" "Summary" "Paragraph one" "Paragraph two"
//
// SERVER defaults to http://localhost:8080.

const [title, summary, ...body] = process.argv.slice(2);
if (!title || !summary || body.length === 0) {
  console.error('usage: npm run publish-article -- "Title" "Summary" "Paragraph" [more paragraphs...]');
  process.exit(2);
}
const token = process.env.ADMIN_TOKEN;
if (!token) {
  console.error('set ADMIN_TOKEN to the token the server printed (or was started with)');
  process.exit(2);
}
const server = process.env.SERVER || 'http://localhost:8080';
const res = await fetch(new URL('/api/articles', server), {
  method: 'POST',
  headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token}` },
  body: JSON.stringify({ title, summary, body }),
});
const out = await res.json();
if (!res.ok) {
  console.error(`HTTP ${res.status}: ${out.error}`);
  process.exit(1);
}
console.log(`published ${server}/articles/${out.article.slug}`);
console.log(`push: ${out.push.sent} sent, ${out.push.failed} failed, ${out.push.removed} expired subscriptions removed`);
