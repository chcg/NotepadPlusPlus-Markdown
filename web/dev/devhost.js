// Stand-in for the C++ host when the page runs in a normal browser.
// Loads ?file=<name> from /__doc/, sends it as a render message, and logs
// everything the page posts back. window.devHost exposes helpers for the
// console: devHost.theme('dark'), devHost.scrollTo(40), devHost.exportHtml().

(() => {
  const dev = globalThis.__mdrDev;
  const params = new URLSearchParams(location.search);
  const file = params.get('file') || 'sample.md';
  const received = [];
  let seq = 0;

  dev.fromPage = (message) => {
    received.push(message);
    console.debug('[page -> host]', message);
  };

  // Like the plugin: the base URL is the document's own folder.
  const baseHref = new URL(`/__doc/${file.replace(/[^/]*$/, '')}`, location.origin).href;

  async function load(reset = true) {
    const text = await (await fetch(`/__doc/${file}`, { cache: 'no-store' })).text();
    dev.toPage({ type: 'render', seq: ++seq, text, baseHref, reset });
  }

  globalThis.devHost = {
    received,
    reload: () => load(false),
    theme: (theme) => dev.toPage({ type: 'settings', theme }),
    settings: (s) => dev.toPage({ type: 'settings', ...s }),
    scrollTo: (line) => dev.toPage({ type: 'scrollTo', line }),
    render: (text) => dev.toPage({ type: 'render', seq: ++seq, text }),
    exportHtml: (theme = 'light') => dev.toPage({ type: 'export', id: ++seq, theme }),
  };

  dev.toPage({ type: 'settings', theme: params.get('theme') === 'dark' ? 'dark' : 'light' });
  load();
})();
