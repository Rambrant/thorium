// The handful of helpers every other module builds the page with.

export const $ = (id) => document.getElementById(id);

export function el(tag, props, ...children) {
  const node = document.createElement(tag);
  Object.assign(node, props || {});
  for (const child of children) node.append(child);
  return node;
}

export const plural = (n, noun) => n + ' ' + noun + (n === 1 ? '' : 's');

const statusEl = $('status');

export function setStatus(text, tone) {
  statusEl.textContent = text;
  statusEl.className = tone || '';
}
