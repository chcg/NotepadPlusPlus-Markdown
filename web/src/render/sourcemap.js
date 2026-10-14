// Tags every block-level token with the source lines it came from, so the
// preview can be scroll-synced with the editor (same idea as VS Code's
// markdown preview). Lines are zero-based, matching Scintilla line numbers.
//
//   data-line      first source line of the block
//   data-line-end  line just past the block (exclusive)

export function sourceMapPlugin(md) {
  md.core.ruler.push('source_map_data_attribute', (state) => {
    for (const token of state.tokens) {
      if (token.map && token.type !== 'inline') {
        token.attrSet('data-line', String(token.map[0]));
        token.attrSet('data-line-end', String(token.map[1]));
        token.attrJoin('class', 'code-line');
      }
    }
  });

  // html_block renders raw HTML and ignores token attributes, so emit an
  // empty marker element in front of it instead.
  const renderHtmlBlock = md.renderer.rules.html_block;
  md.renderer.rules.html_block = (tokens, idx, options, env, self) => {
    const token = tokens[idx];
    const html = renderHtmlBlock(tokens, idx, options, env, self);
    if (!token.map) return html;
    return `<div class="code-line source-marker" data-line="${token.map[0]}" data-line-end="${token.map[1]}"></div>\n${html}`;
  };
}
