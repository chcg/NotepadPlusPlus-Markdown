// Builds the preview page into web/dist, which the plugin ships as
// plugins\MarkdownReader\web\ and serves on https://app.markdownreader.example.
//
//   node build.mjs              production build (minified, strict CSP)
//   node build.mjs --dev        development build for web/dev/serve.mjs (http://localhost)
//   node build.mjs --out <dir>  write somewhere other than web/dist

import { build } from 'esbuild';
import { cp, mkdir, readdir, readFile, rm, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(fileURLToPath(import.meta.url));
const outIndex = process.argv.indexOf('--out');
const dist = outIndex > 0 ? resolve(process.argv[outIndex + 1]) : join(root, 'dist');
const dev = process.argv.includes('--dev');
const modules = join(root, 'node_modules');

// Must match the host names registered in src/WebViewHost.cpp.
const APP_ORIGIN = dev ? 'http://localhost:5178' : 'https://app.markdownreader.example';
const DOC_ORIGIN = dev ? 'http://localhost:5178/__doc' : 'https://doc.markdownreader.example';

const csp = [
  "default-src 'none'",
  `script-src ${APP_ORIGIN}`,
  // Mermaid emits inline <style> in its SVGs.
  `style-src ${APP_ORIGIN} 'unsafe-inline'`,
  `img-src ${APP_ORIGIN} ${DOC_ORIGIN} https: http: data: blob:`,
  `media-src ${DOC_ORIGIN}`,
  `font-src ${APP_ORIGIN} data:`,
  dev ? `connect-src ${APP_ORIGIN}` : "connect-src 'none'",
  `base-uri ${DOC_ORIGIN}/`,
  "form-action 'none'",
  "frame-src 'none'",
  "object-src 'none'",
].join('; ');

await rm(dist, { recursive: true, force: true });
await mkdir(join(dist, 'styles'), { recursive: true });

const result = await build({
  entryPoints: [join(root, 'src/main.js')],
  outfile: join(dist, 'preview.js'),
  bundle: true,
  format: 'iife',
  target: 'chrome120',
  minify: !dev,
  sourcemap: dev ? 'inline' : false,
  legalComments: 'external',
  metafile: true,
  logLevel: 'warning',
});

const html = (await readFile(join(root, 'src/index.html'), 'utf8'))
  .replaceAll('%CSP%', csp)
  .replaceAll('%APP_ORIGIN%', APP_ORIGIN)
  .replaceAll('%DOC_ORIGIN%', DOC_ORIGIN)
  .replace('%DEV_SCRIPT%', dev ? `<script defer src="${APP_ORIGIN}/devhost.js"></script>` : '');
await writeFile(join(dist, 'index.html'), html);

const copies = [
  ['github-markdown-css/github-markdown-light.css', 'styles/github-markdown-light.css'],
  ['github-markdown-css/github-markdown-dark.css', 'styles/github-markdown-dark.css'],
  ['highlight.js/styles/github.css', 'styles/hljs-github.css'],
  ['highlight.js/styles/github-dark.css', 'styles/hljs-github-dark.css'],
  ['mermaid/dist/mermaid.min.js', 'mermaid.min.js'],
];
for (const [from, to] of copies) await cp(join(modules, from), join(dist, to));
await cp(join(root, 'src/styles/preview.css'), join(dist, 'styles/preview.css'));
if (dev) await cp(join(root, 'dev/devhost.js'), join(dist, 'devhost.js'));
await writeFile(join(dist, 'THIRD_PARTY_NOTICES.txt'), await thirdPartyNotices(result.metafile));

console.log(`web: built ${dev ? 'development' : 'production'} page into ${dist}`);

// License texts for everything shipped in web/: the packages esbuild bundled
// into preview.js, the stylesheets we copy, and mermaid.min.js (prebuilt, so
// its runtime dependencies are listed from its package.json).
async function thirdPartyNotices(metafile) {
  const names = new Set(['github-markdown-css', 'highlight.js', 'mermaid']);
  for (const input of Object.keys(metafile.inputs)) {
    const match = input.match(/node_modules[\\/]((?:@[^\\/]+[\\/])?[^\\/]+)/);
    if (match) names.add(match[1].replace(/\\/g, '/'));
  }
  const mermaid = JSON.parse(await readFile(join(modules, 'mermaid/package.json'), 'utf8'));
  for (const dep of Object.keys(mermaid.dependencies ?? {})) {
    if (!dep.startsWith('@types/')) names.add(dep);
  }

  const rule = '='.repeat(78);
  let text = 'Third-party software included in the MarkdownReader preview page (web/).\n\n';
  for (const name of [...names].sort()) {
    const dir = join(modules, name);
    let pkg;
    try {
      pkg = JSON.parse(await readFile(join(dir, 'package.json'), 'utf8'));
    } catch {
      continue; // not installed (optional dependency)
    }
    const files = (await readdir(dir))
      .filter((f) => /^(licen[cs]e|copying|notice)/i.test(f) && !/\.(c?js|mjs|ts|json)$/i.test(f))
      .sort();
    const bodies = await Promise.all(files.map((f) => readFile(join(dir, f), 'utf8')));
    const body = bodies.join('\n\n').trim() || `Licensed under ${pkg.license}. See ${pkg.homepage ?? `https://www.npmjs.com/package/${name}`}`;
    text += `${rule}\n${name} ${pkg.version} (${pkg.license ?? 'see below'})\n${rule}\n\n${body}\n\n`;
  }
  return text;
}
