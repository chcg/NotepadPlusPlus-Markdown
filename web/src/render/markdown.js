import MarkdownIt from 'markdown-it';
import anchor from 'markdown-it-anchor';
import footnote from 'markdown-it-footnote';
import frontMatter from 'markdown-it-front-matter';
import githubAlerts from 'markdown-it-github-alerts';

import { highlightCode } from './highlight.js';
import { sourceMapPlugin } from './sourcemap.js';
import { taskListsPlugin } from './tasklists.js';
import { escapeHtml, hashString, slugify } from './util.js';

// Builds the markdown-it instance used for the preview. The output is
// unsanitized HTML; callers must pass it through sanitize() before display.
export function createMarkdown() {
  const md = new MarkdownIt({ html: true, linkify: true, typographer: false });

  md.use(frontMatter, () => {});
  md.use(footnote);
  md.use(anchor, { slugify, tabIndex: false });
  md.use(githubAlerts);
  md.use(taskListsPlugin);
  md.use(sourceMapPlugin);

  md.renderer.rules.fence = (tokens, idx, options, env, self) =>
    renderFence(tokens, idx, self, md.utils.unescapeAll);
  md.renderer.rules.front_matter = renderFrontMatter;
  // The alerts plugin's renderer drops token attributes (so data-line) and
  // inserts the title unescaped.
  md.renderer.rules.alert_open = renderAlertOpen;

  return md;
}

function renderFence(tokens, idx, self, unescapeAll) {
  const token = tokens[idx];
  const lang = unescapeAll(token.info || '').trim().split(/\s+/)[0].toLowerCase();

  if (lang === 'mermaid') {
    token.attrJoin('class', 'mermaid-block');
    token.attrSet('data-mermaid-hash', hashString(token.content));
    return `<div${self.renderAttrs(token)}><pre class="mermaid-source"><code>${escapeHtml(token.content)}</code></pre></div>\n`;
  }

  const highlighted = highlightCode(token.content, lang);
  const codeClass = lang ? `hljs language-${escapeHtml(lang)}` : 'hljs';
  const body = highlighted ?? escapeHtml(token.content);
  return `<pre${self.renderAttrs(token)}><code class="${codeClass}">${body}</code></pre>\n`;
}

function renderFrontMatter(tokens, idx, options, env, self) {
  const token = tokens[idx];
  return `<details${self.renderAttrs(token)} data-front-matter><summary>Front matter</summary><pre><code>${escapeHtml(token.meta)}</code></pre></details>\n`;
}

function renderAlertOpen(tokens, idx, options, env, self) {
  const token = tokens[idx];
  const { title, type, icon } = token.meta;
  token.attrJoin('class', `markdown-alert markdown-alert-${type}`);
  return `<div${self.renderAttrs(token)}><p class="markdown-alert-title">${icon}${escapeHtml(title)}</p>\n`;
}
