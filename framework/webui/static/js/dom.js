// The handful of helpers every view module builds the page with. The page's
// logic is under model/ -- see model/format.js on the split.

export const $ = (id) => document.getElementById(id);

export function el(tag, props, ...children) {
  const node = document.createElement(tag);
  Object.assign(node, props || {});
  for (const child of children) node.append(child);
  return node;
}

const statusEl = $('status');

export function setStatus(text, tone) {
  statusEl.textContent = text;
  statusEl.className = tone || '';
}
