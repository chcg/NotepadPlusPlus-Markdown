import { JSDOM } from 'jsdom';
import { describe, expect, it } from 'vitest';

import { createMarkdown } from '../src/render/markdown.js';
import { createSanitizer } from '../src/sanitize.js';

const window = new JSDOM('').window;
const md = createMarkdown();

function renderSafe(src, { allowRemoteImages = true } = {}) {
  const sanitizer = createSanitizer(window);
  sanitizer.setAllowRemoteImages(allowRemoteImages);
  const html = sanitizer.sanitize(md.render(src));
  const doc = new JSDOM(`<body>${html}</body>`).window.document;
  return { html, body: doc.body };
}

// Payloads an attacker could put in a .md file. None may survive in an
// executable form.
const XSS_CORPUS = [
  '<script>alert(1)</script>',
  '<img src=x onerror="alert(1)">',
  '<svg><script>alert(1)</script></svg>',
  '<svg onload="alert(1)"></svg>',
  '<a href="javascript:alert(1)">js</a>',
  '[js](javascript:alert(1))',
  '[js](JaVaScRiPt:alert(1))',
  '[ent](&#106;avascript:alert(1))',
  '<a href="data:text/html,<script>alert(1)</script>">data</a>',
  '<a href="vbscript:msgbox(1)">vbs</a>',
  '<iframe src="https://example.com"></iframe>',
  '<object data="x.swf"></object>',
  '<embed src="x.swf">',
  '<form action="https://example.com"><input name=a></form>',
  '<base href="https://evil.example/">',
  '<meta http-equiv="refresh" content="0;url=https://evil.example">',
  '<link rel="stylesheet" href="https://evil.example/x.css">',
  '<style>body{display:none}</style>',
  '<div style="position:fixed;inset:0">overlay</div>',
  '<math><mi xlink:href="javascript:alert(1)">x</mi></math>',
  '<details open ontoggle="alert(1)"><summary>x</summary></details>',
];

describe('sanitizer XSS corpus', () => {
  for (const payload of XSS_CORPUS) {
    it(`neutralizes ${payload}`, () => {
      const { body } = renderSafe(`before\n\n${payload}\n\nafter ${payload}\n`);
      expect(body.querySelector('script, iframe, object, embed, form, base, meta, link, style')).toBeNull();
      // markdown-it leaves rejected links as literal text, which is harmless;
      // what matters is that nothing executable survives in an attribute.
      for (const el of body.querySelectorAll('*')) {
        for (const attr of el.attributes) {
          expect(attr.name.startsWith('on'), `${el.tagName} ${attr.name}`).toBe(false);
          expect(attr.name).not.toBe('style');
          expect(attr.value, `${el.tagName} ${attr.name}`).not.toMatch(/^\s*(javascript|vbscript|data):/i);
        }
      }
    });
  }

  it('strips schemes the host must never launch', () => {
    const { body } = renderSafe('[a](shell:AppsFolder\\x) [b](file:///C:/Windows/System32/calc.exe) [c](ms-settings:privacy) [d](C:\\Windows\\calc.exe)\n');
    // file: is refused by markdown-it itself (left as text); the rest become
    // anchors whose href DOMPurify removes.
    const anchors = [...body.querySelectorAll('a')];
    expect(anchors.map((a) => a.textContent)).toEqual(['a', 'c', 'd']);
    expect(anchors.map((a) => a.getAttribute('href'))).toEqual([null, null, null]);
    expect(body.textContent).toContain('[b](file:///');
  });

  it('keeps ordinary links for the host link policy to judge', () => {
    const { body } = renderSafe('[a](other.md) [b](../docs/x.md#part) [c](https://example.com) [d](mailto:a@b.c) [e](#local)\n');
    const hrefs = [...body.querySelectorAll('a')].map((a) => a.getAttribute('href'));
    expect(hrefs).toEqual(['other.md', '../docs/x.md#part', 'https://example.com', 'mailto:a@b.c', '#local']);
  });

  it('keeps the text of unknown tags (NppMarkdownPanel #194)', () => {
    const { body } = renderSafe('<INSTRUCTIONS>\nDo the thing.\n</INSTRUCTIONS>\n');
    expect(body.textContent).toContain('Do the thing.');
  });

  it('keeps the HTML people use in docs', () => {
    const { body } = renderSafe('<details><summary>More</summary>\n\nText\n</details>\n\n<p align="center"><img src="img/logo.png" width="100" alt="logo"></p>\n\nline<br>break <kbd>Ctrl</kbd>+<kbd>S</kbd> H<sub>2</sub>O\n');
    expect(body.querySelector('details summary').textContent).toBe('More');
    const img = body.querySelector('img');
    expect(img.getAttribute('src')).toBe('img/logo.png');
    expect(img.getAttribute('width')).toBe('100');
    expect(body.querySelector('p[align="center"]')).not.toBeNull();
    expect(body.querySelectorAll('kbd')).toHaveLength(2);
    expect(body.querySelector('sub').textContent).toBe('2');
  });

  it('keeps data-line attributes, task checkboxes and highlighted code', () => {
    const { body } = renderSafe('# T\n\n- [x] done\n\n```js\nlet a = 1;\n```\n');
    expect(body.querySelector('h1').dataset.line).toBe('0');
    expect(body.querySelector('input[type=checkbox][disabled][checked]')).not.toBeNull();
    expect(body.querySelector('pre code .hljs-keyword')).not.toBeNull();
  });

  it('blocks remote images only when asked to', () => {
    const src = '![local](a.png) ![remote](https://tracker.example/p.gif) ![proto](//tracker.example/p.gif)\n';
    const allowed = renderSafe(src).body;
    expect([...allowed.querySelectorAll('img')].map((i) => i.getAttribute('src'))).toEqual(['a.png', 'https://tracker.example/p.gif', '//tracker.example/p.gif']);

    const blocked = renderSafe(src, { allowRemoteImages: false }).body;
    const imgs = [...blocked.querySelectorAll('img')];
    expect(imgs.map((i) => i.getAttribute('src'))).toEqual(['a.png', null, null]);
    expect(imgs[1].dataset.remoteSrc).toBe('https://tracker.example/p.gif');
    expect(imgs[1].classList.contains('remote-image-blocked')).toBe(true);
  });
});
