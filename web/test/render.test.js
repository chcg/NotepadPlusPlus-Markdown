import { JSDOM } from 'jsdom';
import { describe, expect, it } from 'vitest';

import { createMarkdown } from '../src/render/markdown.js';

const md = createMarkdown();
const render = (src) => md.render(src);
const dom = (src) => new JSDOM(`<body>${render(src)}</body>`).window.document.body;

describe('source line mapping', () => {
  it('tags block elements with zero-based data-line and exclusive data-line-end', () => {
    const body = dom('# Title\n\nFirst paragraph\nstill first\n\n- a\n- b\n');
    const h1 = body.querySelector('h1');
    expect(h1.dataset.line).toBe('0');
    expect(h1.dataset.lineEnd).toBe('1');
    const p = body.querySelector('p');
    expect(p.dataset.line).toBe('2');
    expect(p.dataset.lineEnd).toBe('4');
    const items = body.querySelectorAll('li');
    expect([...items].map((li) => li.dataset.line)).toEqual(['5', '6']);
    expect(body.querySelector('ul').classList.contains('code-line')).toBe(true);
  });

  it('puts the line on the <pre> of fenced code', () => {
    const pre = dom('text\n\n```js\nconst a = 1;\nconst b = 2;\n```\n').querySelector('pre');
    expect(pre.dataset.line).toBe('2');
    expect(pre.dataset.lineEnd).toBe('6');
  });

  it('emits a marker in front of raw HTML blocks', () => {
    const body = dom('para\n\n<details>\n<summary>More</summary>\n\nhidden\n</details>\n');
    const marker = body.querySelector('.source-marker');
    expect(marker.dataset.line).toBe('2');
    expect(marker.nextElementSibling.tagName).toBe('DETAILS');
  });

  it('keeps the line on GitHub alerts and escapes their titles', () => {
    const body = dom('> [!WARNING] <b>Careful</b>\n> Body text\n');
    const alert = body.querySelector('.markdown-alert');
    expect(alert.classList.contains('markdown-alert-warning')).toBe(true);
    expect(alert.dataset.line).toBe('0');
    expect(alert.querySelector('.markdown-alert-title').textContent).toContain('<b>Careful</b>');
    expect(alert.querySelector('.markdown-alert-title b')).toBeNull();
  });
});

describe('code blocks', () => {
  it('highlights fences with a known language', () => {
    const code = dom('```python\ndef f():\n    return 1\n```\n').querySelector('pre code');
    expect(code.className).toBe('hljs language-python');
    expect(code.querySelector('.hljs-keyword')).not.toBeNull();
  });

  it('accepts common aliases (ps1, sh, toml, cmd)', () => {
    for (const lang of ['ps1', 'sh', 'toml', 'cmd', 'ts', 'yml', 'html', 'cs']) {
      const code = dom(`\`\`\`${lang}\nx = 1\n\`\`\`\n`).querySelector('pre code');
      expect(code.className, lang).toBe(`hljs language-${lang}`);
    }
  });

  it('escapes fences with no or unknown language and does not auto-detect', () => {
    for (const fence of ['```', '```not-a-language']) {
      const code = dom(`${fence}\n<script>alert(1)</script>\n\`\`\`\n`).querySelector('pre code');
      expect(code.textContent).toBe('<script>alert(1)</script>\n');
      expect(code.querySelector('span')).toBeNull();
    }
  });

  it('turns mermaid fences into placeholders keyed by a source hash', () => {
    const src = 'graph TD\n  A-->B\n';
    const a = dom('```mermaid\n' + src + '```\n').querySelector('.mermaid-block');
    expect(a.dataset.line).toBe('0');
    expect(a.dataset.mermaidHash).toMatch(/^[0-9a-f]{8}$/);
    expect(a.querySelector('.mermaid-source').textContent).toBe(src);
    const b = dom('```mermaid\ngraph TD\n  A-->C\n```\n').querySelector('.mermaid-block');
    expect(b.dataset.mermaidHash).not.toBe(a.dataset.mermaidHash);
  });
});

describe('GitHub-flavored extras', () => {
  it('renders task lists as disabled checkboxes', () => {
    const body = dom('- [ ] todo\n- [x] done\n- [X] also done\n- plain\n');
    const boxes = body.querySelectorAll('input.task-list-item-checkbox');
    expect(boxes).toHaveLength(3);
    expect([...boxes].map((b) => b.checked)).toEqual([false, true, true]);
    expect([...boxes].every((b) => b.disabled)).toBe(true);
    expect(body.querySelector('ul').classList.contains('contains-task-list')).toBe(true);
    expect(body.querySelectorAll('li.task-list-item')).toHaveLength(3);
    expect(body.querySelectorAll('li')[0].textContent.trim()).toBe('todo');
  });

  it('does not treat brackets mid-text as task markers', () => {
    const body = dom('- see [x] here\n- [link](x.md)\n');
    expect(body.querySelector('input')).toBeNull();
  });

  it('renders tables, strikethrough and autolinks', () => {
    const body = dom('| a | b |\n|---|---|\n| 1 | ~~2~~ |\n\nVisit https://example.com today\n');
    expect(body.querySelector('table').dataset.line).toBe('0');
    expect(body.querySelector('s').textContent).toBe('2');
    expect(body.querySelector('a').getAttribute('href')).toBe('https://example.com');
  });

  it('gives headings GitHub-style unique ids', () => {
    const body = dom('# Hello, World!\n\n## Hello, World!\n\n## Café & Crème\n');
    expect([...body.querySelectorAll('h1, h2')].map((h) => h.id)).toEqual(['hello-world', 'hello-world-1', 'café--crème']);
  });

  it('renders footnotes', () => {
    const body = dom('Claim[^1].\n\n[^1]: Source.\n');
    expect(body.querySelector('sup.footnote-ref a').getAttribute('href')).toBe('#fn1');
    expect(body.querySelector('section.footnotes').textContent).toContain('Source.');
  });

  it('shows front matter in a collapsed, escaped details block', () => {
    const body = dom('---\ntitle: <b>Doc</b>\n---\n\n# Body\n');
    const details = body.querySelector('details[data-front-matter]');
    expect(details.dataset.line).toBe('0');
    expect(details.hasAttribute('open')).toBe(false);
    expect(details.querySelector('pre code').textContent).toBe('title: <b>Doc</b>');
    expect(body.querySelector('h1').dataset.line).toBe('4');
  });
});
