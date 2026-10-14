import { escapeHtml } from './render/util.js';

// Serializes the rendered document as a standalone HTML file: stylesheets for
// the requested theme are inlined, Mermaid diagrams are kept as SVG and
// relative image paths are left as written (so the export works when saved
// next to the source document).

function collectCss(theme) {
  const parts = [];
  for (const sheet of document.styleSheets) {
    const owner = sheet.ownerNode;
    const scope = owner?.dataset?.themeSheet ?? (owner?.id === 'custom-css' ? 'both' : null);
    if (scope !== 'both' && scope !== theme) continue;
    try {
      parts.push([...sheet.cssRules].map((rule) => rule.cssText).join('\n'));
    } catch {
      // Unreadable sheet; skip it rather than fail the export.
    }
  }
  return parts.join('\n');
}

export function buildStandaloneHtml(content, theme = 'light') {
  const clone = content.cloneNode(true);
  clone.querySelectorAll('.source-marker').forEach((el) => el.remove());
  clone.querySelectorAll('.mermaid-rendered .mermaid-source').forEach((el) => el.remove());
  clone.querySelectorAll('[data-line]').forEach((el) => {
    el.removeAttribute('data-line');
    el.removeAttribute('data-line-end');
    el.classList.remove('code-line');
    if (el.classList.length === 0) el.removeAttribute('class');
  });

  const title = content.querySelector('h1')?.textContent.trim() || 'Document';
  const html = `<!doctype html>
<html lang="en" data-theme="${theme}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escapeHtml(title)}</title>
<style>
${collectCss(theme)}
</style>
</head>
<body>
<main class="markdown-body">
${clone.innerHTML}
</main>
</body>
</html>
`;
  return { html, title };
}
