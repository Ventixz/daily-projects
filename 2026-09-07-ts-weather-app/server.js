// A static file server with nothing but node:http/fs -- no framework, same
// as this repo's other from-scratch HTTP servers. Used for `make serve` and
// as the origin the e2e tests point a real browser at (ES module imports
// need a real http:// origin; file:// won't do). Serves the project root,
// so dist/app.js (tsc's output) is reachable at /dist/app.js as index.html
// expects.
import http from "node:http";
import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

// This file is also compiled verbatim into dist/server.js (tsc's `allowJs`
// copies it there so tests/e2e/harness.ts can import it with a relative
// specifier), and dist/server.js's own directory is *not* the project root
// -- it's one level deeper. Rather than hardcode that depth (fragile: it'd
// silently break if outDir ever changed), walk up from wherever this file
// actually is until a directory with index.html turns up.
async function findProjectRoot(startDir) {
  let dir = startDir;
  while (true) {
    try {
      await fs.access(path.join(dir, "index.html"));
      return dir;
    } catch {
      const parent = path.dirname(dir);
      if (parent === dir) throw new Error(`findProjectRoot: no index.html found above ${startDir}`);
      dir = parent;
    }
  }
}

const ROOT = await findProjectRoot(path.dirname(fileURLToPath(import.meta.url)));

const CONTENT_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
};

function contentTypeFor(filePath) {
  return CONTENT_TYPES[path.extname(filePath)] ?? "application/octet-stream";
}

async function handle(req, res) {
  const requestPath = decodeURIComponent(new URL(req.url, "http://localhost").pathname);
  const relative = requestPath === "/" ? "index.html" : requestPath.slice(1);
  const resolved = path.normalize(path.join(ROOT, relative));

  if (!resolved.startsWith(ROOT)) {
    res.writeHead(403).end("Forbidden");
    return;
  }

  try {
    const body = await fs.readFile(resolved);
    res.writeHead(200, { "Content-Type": contentTypeFor(resolved) }).end(body);
  } catch {
    res.writeHead(404).end("Not found");
  }
}

export function startServer(port = 0) {
  return new Promise((resolve) => {
    const server = http.createServer((req, res) => {
      handle(req, res).catch((err) => {
        res.writeHead(500).end(String(err));
      });
    });
    server.listen(port, "127.0.0.1", () => resolve(server));
  });
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const port = Number(process.env.PORT) || 8080;
  const server = await startServer(port);
  console.log(`Serving ${ROOT} at http://localhost:${server.address().port}`);
}
