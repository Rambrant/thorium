// --- the catalog ------------------------------------------------------

import { $, el, plural } from './dom.js';
import { state } from './state.js';

const tree = $('tree');

// The catalog as the binary reports it, and the checkbox for each test id.
// Refilled in place by buildTree rather than reassigned, so importers keep
// holding the same objects.
export const catalog = [];
export const testBoxes = new Map();   // id -> { box, verdict, group }
export const groups = [];             // { name, tests, el, box, verdict }

// "group|id|description", split on the first two pipes only -- a
// description may contain one (see webui::parseTestList).
export function parseTestList(text) {
  const tests = [];
  for (const line of text.split('\n')) {
    const a = line.indexOf('|');
    const b = a < 0 ? -1 : line.indexOf('|', a + 1);
    if (b < 0) continue;
    tests.push({ group: line.slice(0, a), id: line.slice(a + 1, b), description: line.slice(b + 1).trim() });
  }
  return tests;
}

export function buildTree(tests) {
  catalog.length = 0;
  catalog.push(...tests);
  testBoxes.clear();
  groups.length = 0;
  tree.replaceChildren();

  if (tests.length === 0) {
    tree.append(el('div', { className: 'note', textContent: 'This suite reports no tests.' }));
    updateSelection();
    return;
  }

  // Catalog order, grouped as it arrives -- the same run of lines the old
  // console's fillTree walked, not a sort, because the catalog's order is
  // the run order.
  let current = null;
  for (const test of tests) {
    if (!current || current.name !== test.group) {
      current = { name: test.group, tests: [] };
      groups.push(current);
    }
    current.tests.push(test);
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
      testBoxes.set(test.id, { box: testBox, verdict, group });
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

// Keeps each group's box in step with its tests: checked, unchecked, or the
// third state when only some are ticked.
export function updateSelection() {
  for (const group of groups) {
    const ticked = group.tests.filter((t) => testBoxes.get(t.id).box.checked).length;
    group.box.checked = ticked === group.tests.length;
    group.box.indeterminate = ticked > 0 && ticked < group.tests.length;
  }
  const selected = selectedIds().length;
  $('selCount').textContent = selected + ' of ' + plural(catalog.length, 'test') + ' selected';
  $('run').disabled = state.running || catalog.length === 0 || selected === 0;
}

// A group's row summarises its tests' verdicts: running while any of them
// is, then the number that failed, or PASS once every test of it that ran
// passed. Blank for a group none of whose tests have run.
export function refreshGroupVerdict(group) {
  const states = group.tests.map((t) => testBoxes.get(t.id).verdict.textContent);
  const failed = states.filter((v) => v === 'FAIL').length;
  const v = group.verdict;
  if (states.includes('...')) { v.textContent = '...'; v.className = 'verdict running'; }
  else if (failed) { v.textContent = failed + ' FAIL'; v.className = 'verdict fail'; }
  else if (states.includes('PASS')) { v.textContent = 'PASS'; v.className = 'verdict pass'; }
  else { v.textContent = ''; v.className = 'verdict'; }
}

function selectedIds() {
  return catalog.filter((t) => testBoxes.get(t.id).box.checked).map((t) => t.id);
}

// Everything ticked is an empty selection, which is an absent --select.
// Not the same as naming every id: an operator who ticked "all" means all,
// and a page that froze today's list into --select would quietly keep
// running yesterday's suite the day a test is added. See RunRequest::Selection.
export function selection() {
  const ids = selectedIds();
  return ids.length === catalog.length ? [] : ids;
}

function setAll(checked) {
  for (const { box } of testBoxes.values()) box.checked = checked;
  updateSelection();
}

$('selAll').addEventListener('click', () => setAll(true));
$('selNone').addEventListener('click', () => setAll(false));
$('expandAll').addEventListener('click', () => groups.forEach((g) => g.el.classList.remove('collapsed')));
$('collapseAll').addEventListener('click', () => groups.forEach((g) => g.el.classList.add('collapsed')));
