// The catalog and what is selected from it, without the tree that shows it.

import { plural } from './format.js';

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

// Catalog order, grouped as it arrives -- the same run of lines the old
// console's fillTree walked, not a sort, because the catalog's order is
// the run order. A group that reappears later is a second group, not merged
// into the first: merging would reorder the run.
export function groupTests(tests) {
  const groups = [];
  let current = null;
  for (const test of tests) {
    if (!current || current.name !== test.group) {
      current = { name: test.group, tests: [] };
      groups.push(current);
    }
    current.tests.push(test);
  }
  return groups;
}

// A group's box: checked, unchecked, or the third state when only some of
// its tests are ticked.
export function tickState(ticked, total) {
  return { checked: ticked === total, indeterminate: ticked > 0 && ticked < total };
}

export function selectedIds(tests, isTicked) {
  return tests.filter((t) => isTicked(t.id)).map((t) => t.id);
}

// Everything ticked is an empty selection, which is an absent --select.
// Not the same as naming every id: an operator who ticked "all" means all,
// and a page that froze today's list into --select would quietly keep
// running yesterday's suite the day a test is added. See RunRequest::Selection.
export function selection(tests, isTicked) {
  const ids = selectedIds(tests, isTicked);
  return ids.length === tests.length ? [] : ids;
}

export function selectionSummary(selected, total) {
  return selected + ' of ' + plural(total, 'test') + ' selected';
}

// A test's verdict in the tree: '' before it has run, then 'running',
// 'pass' or 'fail' -- the name is also its CSS class.
export const verdictLabel = { '': '', running: '...', pass: 'PASS', fail: 'FAIL' };

// A group's row summarises its tests' verdicts: running while any of them
// is, then the number that failed, or PASS once every test of it that ran
// passed. Blank for a group none of whose tests have run.
export function groupVerdict(states) {
  const failed = states.filter((s) => s === 'fail').length;
  if (states.includes('running')) return { text: '...', tone: 'running' };
  if (failed) return { text: failed + ' FAIL', tone: 'fail' };
  if (states.includes('pass')) return { text: 'PASS', tone: 'pass' };
  return { text: '', tone: '' };
}
