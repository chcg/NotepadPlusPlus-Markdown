import morphdom from 'morphdom';

import { onHostMessage, post } from './bridge.js';
import { buildStandaloneHtml } from './export.js';
import { renderMermaidBlocks } from './mermaid-loader.js';
import { createMarkdown } from './render/markdown.js';
import { createSanitizer } from './sanitize.js';
import { collectEntries, lineForTop, topForLine } from './scrollsync.js';

const APP_ORIGIN = document.documentElement.dataset.appOrigin;
const content = document.getElementById('content');
const customCss = document.getElementById('custom-css');
const base = document.querySelector('base');

const md = createMarkdown();
const sanitizer = createSanitizer(window);

const settings = {
  theme: 'light',
  allowRemoteImages: true,
  mermaid: true,
  syncScroll: true,
};

let lastSeq = -1;
let lastText = null;

// ---- rendering --------------------------------------------------------------

function render(text) {
  const html = sanitizer.sanitize(md.render(text));
  const next = content.cloneNode(false);
  next.innerHTML = html;

  morphdom(content, next, {
    onBeforeElUpdated(fromEl, toEl) {
      // Keep already-rendered diagrams whose source did not change; only
      // their line attributes move.
      if (fromEl.classList.contains('mermaid-block') && fromEl.dataset.mermaidHash === toEl.dataset.mermaidHash) {
        fromEl.dataset.line = toEl.dataset.line;
        fromEl.dataset.lineEnd = toEl.dataset.lineEnd;
        return false;
      }
      // Don't collapse <details> the reader opened.
      if (fromEl.tagName === 'DETAILS' && fromEl.open) toEl.setAttribute('open', '');
      return !fromEl.isEqualNode(toEl);
    },
  });

  invalidateEntries();
  if (settings.mermaid) refreshMermaid();
}

async function refreshMermaid() {
  const changed = await renderMermaidBlocks(content, { theme: settings.theme, appOrigin: APP_ORIGIN });
  if (changed) invalidateEntries();
}

// ---- theme / settings -----------------------------------------------------------

// Printing always uses the light sheets: when dark is active, the light ones
// are switched to media="print" and the dark ones to media="screen".
function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
  for (const link of document.querySelectorAll('link[data-theme-sheet]')) {
    const scope = link.dataset.themeSheet;
    if (scope === 'both') continue;
    if (theme === 'dark') link.media = scope === 'dark' ? 'screen' : 'print';
    else link.media = scope === 'light' ? 'all' : 'not all';
  }
}

function applySettings(next) {
  const themeChanged = next.theme && next.theme !== settings.theme;
  const remoteChanged = 'allowRemoteImages' in next && next.allowRemoteImages !== settings.allowRemoteImages;
  Object.assign(settings, {
    theme: next.theme === 'dark' ? 'dark' : next.theme === 'light' ? 'light' : settings.theme,
    allowRemoteImages: next.allowRemoteImages ?? settings.allowRemoteImages,
    mermaid: next.mermaid ?? settings.mermaid,
    syncScroll: next.syncScroll ?? settings.syncScroll,
  });
  if (typeof next.customCss === 'string') customCss.textContent = next.customCss;

  applyTheme(settings.theme);
  sanitizer.setAllowRemoteImages(settings.allowRemoteImages);
  if (remoteChanged && lastText !== null) render(lastText);
  else if (themeChanged && settings.mermaid) refreshMermaid();
  invalidateEntries();
}

// ---- scroll sync ------------------------------------------------------------------

let entries = null;
let suppressScrollUntil = 0;
let pendingReveal = false;

function invalidateEntries() {
  entries = null;
}

function getEntries() {
  if (!entries) entries = collectEntries(content, window.scrollY);
  return entries;
}

function scrollToLine(line) {
  suppressScrollUntil = performance.now() + 200;
  window.scrollTo({ top: Math.max(0, topForLine(getEntries(), line)), behavior: 'instant' });
}

// Throttled with a timer rather than requestAnimationFrame: rAF stops firing
// while the WebView is hidden, which would silently drop the last scroll.
window.addEventListener('scroll', () => {
  if (!settings.syncScroll || pendingReveal) return;
  if (performance.now() < suppressScrollUntil) return;
  pendingReveal = true;
  setTimeout(() => {
    pendingReveal = false;
    if (performance.now() < suppressScrollUntil) return;
    const line = lineForTop(getEntries(), window.scrollY);
    post({ type: 'reveal', line: Math.floor(line) });
  }, 40);
}, { passive: true });

window.addEventListener('resize', invalidateEntries);
// Images and opened <details> change the layout after rendering.
content.addEventListener('load', invalidateEntries, true);
content.addEventListener('toggle', invalidateEntries, true);

// ---- interaction ------------------------------------------------------------------

function scrollToFragment(fragment) {
  let id = fragment;
  try {
    id = decodeURIComponent(fragment);
  } catch {
    // keep the raw fragment
  }
  const target = document.getElementById(id) ?? document.getElementsByName(id)[0];
  target?.scrollIntoView({ block: 'start' });
}

// Every link click is handled here: in-page anchors scroll, everything else
// goes to the host, which decides (browser / Notepad++ / blocked). The host
// also cancels any navigation that slips through.
function onLinkActivated(event) {
  const anchor = event.target.closest?.('a');
  if (!anchor || !content.contains(anchor)) return;
  event.preventDefault();
  const href = anchor.getAttribute('href') ?? anchor.getAttribute('xlink:href');
  if (!href) return;
  if (href.startsWith('#')) {
    scrollToFragment(href.slice(1));
    return;
  }
  // Resolved against the document's folder (the <base>), so ../ works.
  let url;
  try {
    url = new URL(href, document.baseURI).href;
  } catch {
    return;
  }
  post({ type: 'link', href, url });
}

// Each document gets a base URL naming its own folder, so its relative
// images and links resolve there. Returns true if it changed.
function setBase(href) {
  if (typeof href !== 'string' || !href || base.getAttribute('href') === href) return false;
  base.setAttribute('href', href);
  return true;
}
document.addEventListener('click', onLinkActivated);
document.addEventListener('auxclick', onLinkActivated);

document.addEventListener('dblclick', (event) => {
  if (!event.target.closest?.('[data-line]')) return;
  const line = lineForTop(getEntries(), event.pageY);
  window.getSelection()?.removeAllRanges();
  post({ type: 'gotoLine', line: Math.floor(line) });
});

// ---- host messages ------------------------------------------------------------------

onHostMessage((message) => {
  switch (message.type) {
    case 'render': {
      if (typeof message.text !== 'string' || !(message.seq > lastSeq)) return;
      lastSeq = message.seq;
      lastText = message.text;
      // Images already in the DOM don't re-resolve when the base changes,
      // so start from an empty document.
      if (setBase(message.baseHref)) content.replaceChildren();
      render(message.text);
      if (message.reset) {
        if (Number.isFinite(message.scrollLine)) scrollToLine(message.scrollLine);
        else window.scrollTo({ top: 0, behavior: 'instant' });
      }
      post({ type: 'rendered', seq: message.seq });
      break;
    }
    case 'settings':
      applySettings(message);
      break;
    case 'scrollTo':
      if (settings.syncScroll && Number.isFinite(message.line)) scrollToLine(message.line);
      break;
    case 'export': {
      const theme = message.theme === 'dark' ? 'dark' : 'light';
      const { html, title } = buildStandaloneHtml(content, theme);
      post({ type: 'exportResult', id: message.id, html, title });
      break;
    }
    default:
      break;
  }
});

applyTheme(settings.theme);
post({ type: 'ready' });
