// Message channel between the page and the plugin (C++ host).
//
// Host -> page (PostWebMessageAsJson):
//   { type: 'render',   seq, text, baseHref, reset?, scrollLine? }
//   { type: 'settings', theme: 'light'|'dark', allowRemoteImages, mermaid, syncScroll, customCss }
//   { type: 'scrollTo', line }
//   { type: 'export',   id, theme }
//
// Page -> host (chrome.webview.postMessage, received as WebMessageAsJson):
//   { type: 'ready' }
//   { type: 'rendered', seq }
//   { type: 'reveal',   line }      preview was scrolled by the user
//   { type: 'gotoLine', line }      double-click: jump to source
//   { type: 'link',     href, url } url = href resolved against the document; host applies its link policy
//   { type: 'exportResult', id, html, title }
//
// The C++ side validates every incoming message; keep the two in sync
// (src/core/PageMessages.h).

const webview = globalThis.chrome?.webview;
const handlers = new Set();

export const isHosted = Boolean(webview);

export function post(message) {
  if (webview) webview.postMessage(message);
  else globalThis.__mdrDev?.fromPage?.(message);
}

export function onHostMessage(handler) {
  handlers.add(handler);
}

function dispatch(message) {
  if (!message || typeof message.type !== 'string') return;
  for (const handler of handlers) handler(message);
}

if (webview) {
  webview.addEventListener('message', (event) => dispatch(event.data));
} else {
  // Development outside WebView2 (see web/dev/devhost.js).
  globalThis.__mdrDev = { ...globalThis.__mdrDev, toPage: dispatch };
}
