// Minimal static file server (node:http only). ES modules need a real http://
// origin, so this backs both `npm run serve` and the browser tests.
import http from "node:http";
import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
};

async function handle(req, res) {
  const pathname = decodeURIComponent(new URL(req.url, "http://localhost").pathname);
  const resolved = path.join(ROOT, pathname === "/" ? "index.html" : pathname);
  // Compare against ROOT + separator so "/root-evil" can't pass a prefix check for "/root".
  if (!resolved.startsWith(ROOT + path.sep)) return res.writeHead(403).end("Forbidden");
  try {
    const body = await fs.readFile(resolved);
    res.writeHead(200, { "Content-Type": TYPES[path.extname(resolved)] ?? "application/octet-stream" }).end(body);
  } catch {
    res.writeHead(404).end("Not found");
  }
}

export function startServer(port = 0) {
  return new Promise((resolve) => {
    const server = http.createServer((req, res) => handle(req, res).catch((e) => res.writeHead(500).end(String(e))));
    server.listen(port, "127.0.0.1", () => resolve(server));
  });
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const server = await startServer(Number(process.env.PORT) || 8080);
  console.log(`Serving at http://localhost:${server.address().port}`);
}
