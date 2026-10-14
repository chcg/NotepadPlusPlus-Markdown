// GFM task lists: "- [ ] todo" / "- [x] done".
// The npm task-list plugins are unmaintained, so this is a small local rule.
// Checkboxes are rendered disabled; the preview is read-only.

const MARKER = /^\[([ xX])\][ \t]/;

export function taskListsPlugin(md) {
  md.core.ruler.after('inline', 'task_lists', (state) => {
    const tokens = state.tokens;
    for (let i = 2; i < tokens.length; i++) {
      const inline = tokens[i];
      if (inline.type !== 'inline') continue;
      if (tokens[i - 1].type !== 'paragraph_open' || tokens[i - 2].type !== 'list_item_open') continue;

      const match = MARKER.exec(inline.content);
      const first = inline.children && inline.children[0];
      if (!match || !first || first.type !== 'text' || !MARKER.test(first.content)) continue;

      const checked = match[1] !== ' ';
      first.content = first.content.slice(match[0].length);
      inline.content = inline.content.slice(match[0].length);

      const checkbox = new state.Token('html_inline', '', 0);
      checkbox.content = `<input class="task-list-item-checkbox" type="checkbox" disabled${checked ? ' checked' : ''}> `;
      inline.children.unshift(checkbox);

      tokens[i - 2].attrJoin('class', 'task-list-item');
      const listOpen = findParentListOpen(tokens, i - 2);
      if (listOpen) listOpen.attrJoin('class', 'contains-task-list');
    }
  });
}

function findParentListOpen(tokens, listItemIndex) {
  const level = tokens[listItemIndex].level - 1;
  for (let j = listItemIndex - 1; j >= 0; j--) {
    const t = tokens[j];
    if (t.level === level && (t.type === 'bullet_list_open' || t.type === 'ordered_list_open')) {
      return (t.attrGet('class') || '').includes('contains-task-list') ? null : t;
    }
  }
  return null;
}
