import createDOMPurify from 'dompurify';

// Everything markdown-it produces goes through here before it touches the DOM.
// The host (C++) also cancels every navigation and applies its own link policy,
// so this is one of several layers, not the only one.

const FORBID_TAGS = ['style', 'link', 'meta', 'base', 'form', 'iframe', 'frame', 'frameset', 'object', 'embed', 'applet', 'portal'];
// GitHub strips inline styles too; they could also be used to overlay fake UI.
const FORBID_ATTR = ['style'];

const REMOTE_URL = /^(https?:)?\/\//i;

export function createSanitizer(win = globalThis.window) {
  const purify = createDOMPurify(win);
  let allowRemoteImages = true;

  // Remote images are optional (privacy / tracking pixels). Blocked ones keep
  // their URL in data-remote-src so the placeholder can show where they point.
  purify.addHook('afterSanitizeAttributes', (node) => {
    if (allowRemoteImages || node.nodeName !== 'IMG') return;
    const src = node.getAttribute('src') || '';
    if (!REMOTE_URL.test(src)) return;
    node.removeAttribute('src');
    node.removeAttribute('srcset');
    node.setAttribute('data-remote-src', src);
    node.classList.add('remote-image-blocked');
  });

  return {
    setAllowRemoteImages(value) {
      allowRemoteImages = Boolean(value);
    },
    sanitize(html) {
      return purify.sanitize(html, {
        USE_PROFILES: { html: true, svg: true, svgFilters: true },
        FORBID_TAGS,
        FORBID_ATTR,
        KEEP_CONTENT: true,
        ALLOW_DATA_ATTR: true,
      });
    },
  };
}
