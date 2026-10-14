// Development server: serves web/dist (built with `npm run build:dev`) and
// exposes tests/manual as the "document folder" under /__doc/, mirroring the
// two origins the plugin uses inside WebView2.
//
//   node dev/serve.mjs   ->  http://localhost:5178/?file=sample.md

import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { createServer } from 'node:http';
import { dirname, extname, join, normalize, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const distRoot = join(here, '..', 'dist');
const docRoot = join(here, '..', '..', 'tests', 'manual');
const PORT = 5178;

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.md': 'text/markdown; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.gif': 'image/gif',
  '.webp': 'image/webp',
};

function resolveUnder(base, urlPath) {
  const full = normalize(join(base, decodeURIComponent(urlPath)));
  return full === base || full.startsWith(base + sep) ? full : null;
}

createServer(async (req, res) => {
  const { pathname } = new URL(req.url, `http://localhost:${PORT}`);
  const file = pathname.startsWith('/__doc/')
    ? resolveUnder(docRoot, pathname.slice('/__doc/'.length))
    : resolveUnder(distRoot, pathname === '/' ? 'index.html' : pathname.slice(1));

  try {
    if (!file || !(await stat(file)).isFile()) throw new Error('not found');
    res.writeHead(200, { 'Content-Type': TYPES[extname(file).toLowerCase()] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
    createReadStream(file).pipe(res);
  } catch {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('404');
  }
}).listen(PORT, () => console.log(`dev server on http://localhost:${PORT}/?file=sample.md`));
