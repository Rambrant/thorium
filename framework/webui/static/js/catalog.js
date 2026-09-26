// --- the catalog ------------------------------------------------------
//
// The tree of checkboxes. What goes in it, what a tick means and what a
// group's verdict says are model/catalog.js's.

import { $, el } from './dom.js';
import { state } from './state.js';
import { plural } from './model/format.js';
import * as model from './model/catalog.js';

const tree = $('tree');

// The catalog as the binary reports it, and each test's row in the tree.
// Refilled in place by buildTree rather than reassigned, so importers keep
// holding the same objects.
export const catalog = [];
export const testBoxes = new Map();   // id -> { box, verdict, state, group }
export const groups = [];             // { name, tests, el, box, verdict }

const isTicked = (id) => testBoxes.get(id).box.checked;

export function buildTree(tests) {
  catalog.length = 0;
  catalog.push(...tests);
  testBoxes.clear();
  groups.length = 0;
  groups.push(...model.groupTests(tests));
  tree.replaceChildren();

  if (tests.length === 0) {
    tree.append(el('div', { className: 'note', textContent: 'This suite reports no tests.' }));
    updateSelection();
    return;
  }

  for (const group of groups) {
    const box = el('input', { type: 'checkbox', checked: true });
    const caret = el('button', { className: 'caret', textContent: '▾', title: 'Expand or collapse' });
    const list = el('ul', { className: 'tests' });
    const groupVerdict = el('span', { className: 'verdict' });

    // Collapsed to begin with: a real catalog runs to hundreds of tests, and
    // a tree that opens fully expanded is one screen of groups buried in
    // several of tests. The group row carries its tests' verdict (see
    // refreshGroupVerdict) so a failure is visible without opening it.
    const node = el('div', { className: 'group collapsed' },
      el('div', { className: 'group-row' }, caret,
        el('label', { className: 'row-label' }, box,
          el('span', { textContent: group.name }),
          el('span', { className: 'count', textContent: plural(group.tests.length, 'test') })),
        groupVerdict),
      list);

    caret.addEventListener('click', () => node.classList.toggle('collapsed'));

    // Ticking a group ticks every test in it -- which is what "run this
    // group" means, and what an operator reading the tree assumes it means.
    box.addEventListener('change', () => {
      for (const test of group.tests) testBoxes.get(test.id).box.checked = box.checked;
      updateSelection();
    });

    for (const test of group.tests) {
      const testBox = el('input', { type: 'checkbox', checked: true });
      const verdict = el('span', { className: 'verdict' });
      testBox.addEventListener('change', updateSelection);
      list.append(el('li', { className: 'test-row', title: test.description },
        el('label', { className: 'row-label' }, testBox,
          el('span', { className: 'tid', textContent: test.id }),
          el('span', { className: 'desc', textContent: test.description })),
        verdict));
      testBoxes.set(test.id, { box: testBox, verdict, state: '', group });
    }

    group.el = node;
    group.box = box;
    group.verdict = groupVerdict;
    tree.append(node);
  }

  updateSelection();
}

export function showCatalogError(text) {
  tree.replaceChildren(el('div', { className: 'note', textContent: text }));
}

export function updateSelection() {
  for (const group of groups) {
    const ticked = group.tests.filter((t) => isTicked(t.id)).length;
    Object.assign(group.box, model.tickState(ticked, group.tests.length));
  }
  const selected = model.selectedIds(catalog, isTicked).length;
  $('selCount').textContent = model.selectionSummary(selected, catalog.length);
  $('run').disabled = state.running || catalog.length === 0 || selected === 0;
}

export function selection() {
  return model.selection(catalog, isTicked);
}

// One test's verdict, and its group's with it. Ignored for a test the tree
// does not have: the stream is the binary's, the tree is what it said earlier.
export function setVerdict(id, verdictState) {
  const entry = testBoxes.get(id);
  if (!entry) return;
  entry.state = verdictState;
  entry.verdict.textContent = model.verdictLabel[verdictState];
  entry.verdict.className = 'verdict ' + verdictState;
  refreshGroupVerdict(entry.group);
}

export function clearVerdicts() {
  for (const id of testBoxes.keys()) setVerdict(id, '');
}

function refreshGroupVerdict(group) {
  const { text, tone } = model.groupVerdict(group.tests.map((t) => testBoxes.get(t.id).state));
  group.verdict.textContent = text;
  group.verdict.className = 'verdict ' + tone;
}

function setAll(checked) {
  for (const { box } of testBoxes.values()) box.checked = checked;
  updateSelection();
}

$('selAll').addEventListener('click', () => setAll(true));
$('selNone').addEventListener('click', () => setAll(false));
$('expandAll').addEventListener('click', () => groups.forEach((g) => g.el.classList.remove('collapsed')));
$('collapseAll').addEventListener('click', () => groups.forEach((g) => g.el.classList.add('collapsed')));
