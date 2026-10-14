// Maps between source lines and vertical positions in the preview.
//
// Entries come from elements carrying data-line / data-line-end (see
// render/sourcemap.js): { line, endLine, top, height } with top in document
// coordinates. The math is kept DOM-free so it can be unit tested.

export function collectEntries(root, scrollY = 0) {
  const entries = [];
  for (const el of root.querySelectorAll('[data-line]')) {
    const line = Number(el.getAttribute('data-line'));
    const endLine = Number(el.getAttribute('data-line-end') ?? line + 1);
    if (!Number.isFinite(line)) continue;
    const rect = el.getBoundingClientRect();
    let height = rect.height;
    if (el.classList.contains('source-marker')) {
      // The marker is zero-height; it stands for the raw HTML element after it.
      const html = el.nextElementSibling;
      if (html && !html.hasAttribute('data-line')) height = Math.max(0, html.getBoundingClientRect().bottom - rect.top);
    } else if (rect.height === 0 && rect.width === 0) {
      continue; // not rendered (e.g. inside a closed <details>)
    }
    entries.push({ line, endLine: Math.max(endLine, line + 1), top: rect.top + scrollY, height });
  }
  entries.sort((a, b) => a.line - b.line || a.top - b.top);
  return entries;
}

// Innermost block containing the line, e.g. the paragraph inside a list item.
function containing(entries, line) {
  let best = null;
  for (const e of entries) {
    if (e.line > line) break;
    if (line < e.endLine && (!best || e.endLine - e.line <= best.endLine - best.line)) best = e;
  }
  return best;
}

// Vertical position (document coordinates) where a source line starts.
// Fractional lines are allowed.
export function topForLine(entries, line) {
  if (entries.length === 0) return 0;

  const inside = containing(entries, Math.floor(line));
  if (inside) {
    const progress = (line - inside.line) / (inside.endLine - inside.line);
    return inside.top + progress * inside.height;
  }

  // In a gap between blocks (blank lines): interpolate between the end of the
  // previous block and the start of the next one.
  let prev = null;
  let next = null;
  for (const e of entries) {
    if (e.endLine <= line) {
      if (!prev || e.endLine > prev.endLine || (e.endLine === prev.endLine && e.top + e.height > prev.top + prev.height)) prev = e;
    } else if (e.line > line && (!next || e.line < next.line)) {
      next = e;
    }
  }
  if (!prev) return next ? next.top : 0;
  const prevBottom = prev.top + prev.height;
  if (!next) return prevBottom;
  const progress = (line - prev.endLine) / Math.max(1, next.line - prev.endLine);
  return prevBottom + progress * (next.top - prevBottom);
}

// Inverse of topForLine: which source line is at document position y.
export function lineForTop(entries, y) {
  if (entries.length === 0) return 0;

  // Innermost element spanning y.
  let inside = null;
  for (const e of entries) {
    if (y >= e.top && y < e.top + e.height && e.height > 0) {
      if (!inside || e.height <= inside.height) inside = e;
    }
  }
  if (inside) {
    const progress = (y - inside.top) / inside.height;
    return inside.line + progress * (inside.endLine - inside.line);
  }

  let prev = null;
  let next = null;
  for (const e of entries) {
    const bottom = e.top + e.height;
    if (bottom <= y) {
      if (!prev || bottom > prev.top + prev.height) prev = e;
    } else if (e.top > y && (!next || e.top < next.top)) {
      next = e;
    }
  }
  if (!prev) return next ? next.line : 0;
  if (!next) return prev.endLine;
  const prevBottom = prev.top + prev.height;
  const progress = (y - prevBottom) / Math.max(1, next.top - prevBottom);
  return prev.endLine + progress * (next.line - prev.endLine);
}
