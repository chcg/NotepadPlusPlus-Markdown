// Renders ```mermaid blocks. mermaid.min.js is ~5.5 MB, so it is only loaded
// the first time a document actually contains a diagram.
//
// Each .mermaid-block keeps its source in a hidden <pre class="mermaid-source">
// and gets a sibling .mermaid-svg with the diagram. SVGs are cached by
// theme + source hash so typing elsewhere in the document is cheap.

const cache = new Map();
const CACHE_LIMIT = 200;
let loader = null;
let configuredTheme = null;
let idCounter = 0;

function loadMermaid(appOrigin) {
  if (!loader) {
    loader = new Promise((resolve, reject) => {
      const script = document.createElement('script');
      script.src = `${appOrigin}/mermaid.min.js`;
      script.onload = () => (globalThis.mermaid ? resolve(globalThis.mermaid) : reject(new Error('mermaid did not load')));
      script.onerror = () => {
        loader = null;
        reject(new Error('mermaid.min.js could not be loaded'));
      };
      document.head.appendChild(script);
    });
  }
  return loader;
}

export function blockKey(block, theme) {
  return `${theme}:${block.dataset.mermaidHash}`;
}

// Returns true if any diagram changed (so scroll positions must be re-measured).
export async function renderMermaidBlocks(root, { theme, appOrigin }) {
  const pending = [...root.querySelectorAll('.mermaid-block')].filter(
    (block) => block.dataset.renderedKey !== blockKey(block, theme),
  );
  if (pending.length === 0) return false;

  let mermaid;
  try {
    mermaid = await loadMermaid(appOrigin);
  } catch (err) {
    for (const block of pending) showError(block, err.message);
    return true;
  }

  if (configuredTheme !== theme) {
    mermaid.initialize({
      startOnLoad: false,
      securityLevel: 'strict',
      theme: theme === 'dark' ? 'dark' : 'default',
    });
    configuredTheme = theme;
  }

  for (const block of pending) {
    const key = blockKey(block, theme);
    const source = block.querySelector('.mermaid-source')?.textContent ?? '';
    let svg = cache.get(key);
    let error = null;

    if (svg === undefined) {
      const id = `mermaid-${block.dataset.mermaidHash}-${++idCounter}`;
      try {
        ({ svg } = await mermaid.render(id, source));
        cache.set(key, svg);
        if (cache.size > CACHE_LIMIT) cache.delete(cache.keys().next().value);
      } catch (err) {
        error = err?.message || String(err);
        // A failed render leaves its scratch element behind.
        document.getElementById(`d${id}`)?.remove();
        document.getElementById(id)?.remove();
      }
    }

    // The document may have been re-rendered while we awaited.
    if (!block.isConnected || blockKey(block, theme) !== key) continue;

    if (error) {
      showError(block, error);
    } else {
      block.querySelector('.mermaid-error')?.remove();
      let target = block.querySelector('.mermaid-svg');
      if (!target) {
        target = document.createElement('div');
        target.className = 'mermaid-svg';
        block.appendChild(target);
      }
      target.innerHTML = svg;
      block.classList.add('mermaid-rendered');
    }
    block.dataset.renderedKey = key;
  }
  return true;
}

function showError(block, message) {
  block.querySelector('.mermaid-svg')?.remove();
  block.classList.remove('mermaid-rendered');
  let box = block.querySelector('.mermaid-error');
  if (!box) {
    box = document.createElement('div');
    box.className = 'mermaid-error';
    block.appendChild(box);
  }
  box.textContent = `Mermaid: ${message}`;
}
