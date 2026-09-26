import { test, assertEqual, assertFalse, assertTrue } from './harness.js';
import * as catalog from '../../static/js/model/catalog.js';

test('parseTestList: splits on the first two pipes only', () => {
  const tests = catalog.parseTestList('Power|P1|Rail comes up\nPower|P2|A | B, both\n');
  assertEqual(tests, [
    { group: 'Power', id: 'P1', description: 'Rail comes up' },
    { group: 'Power', id: 'P2', description: 'A | B, both' },
  ]);
});

test('parseTestList: skips lines without two pipes, trims the description', () => {
  assertEqual(catalog.parseTestList('\nnoise\nG|only-one-pipe\nG|T|  padded  \r'), [
    { group: 'G', id: 'T', description: 'padded' },
  ]);
});

test('groupTests: catalog order, and a group that comes back is a second group', () => {
  const tests = [
    { group: 'A', id: 'a1' }, { group: 'A', id: 'a2' }, { group: 'B', id: 'b1' }, { group: 'A', id: 'a3' },
  ];
  const groups = catalog.groupTests(tests);
  assertEqual(groups.map((g) => g.name), ['A', 'B', 'A']);
  assertEqual(groups.map((g) => g.tests.map((t) => t.id)), [['a1', 'a2'], ['b1'], ['a3']]);
});

test('tickState: all, none and some', () => {
  assertEqual(catalog.tickState(3, 3), { checked: true, indeterminate: false });
  assertEqual(catalog.tickState(0, 3), { checked: false, indeterminate: false });
  assertEqual(catalog.tickState(1, 3), { checked: false, indeterminate: true });
});

const tests = [{ id: 'T1' }, { id: 'T2' }, { id: 'T3' }];

test('selection: everything ticked is an empty selection, not every id', () => {
  assertEqual(catalog.selection(tests, () => true), []);
});

test('selection: a partial tick names the ticked ids, in catalog order', () => {
  const ticked = new Set(['T3', 'T1']);
  assertEqual(catalog.selection(tests, (id) => ticked.has(id)), ['T1', 'T3']);
});

test('selection: nothing ticked is also a list of ids -- the empty one is taken', () => {
  // Run is disabled with nothing selected, so this never reaches the server;
  // it is here to pin down that "none" and "all" are told apart by selectedIds.
  assertEqual(catalog.selectedIds(tests, () => false), []);
  assertEqual(catalog.selectedIds(tests, () => true).length, 3);
});

test('selectionSummary', () => {
  assertEqual(catalog.selectionSummary(2, 5), '2 of 5 tests selected');
  assertEqual(catalog.selectionSummary(1, 1), '1 of 1 test selected');
});

test('groupVerdict: running wins, then failures, then PASS, else blank', () => {
  assertEqual(catalog.groupVerdict(['pass', 'running', 'fail']), { text: '...', tone: 'running' });
  assertEqual(catalog.groupVerdict(['fail', 'pass', 'fail']), { text: '2 FAIL', tone: 'fail' });
  assertEqual(catalog.groupVerdict(['pass', '']), { text: 'PASS', tone: 'pass' });
  assertEqual(catalog.groupVerdict(['', '']), { text: '', tone: '' });
});

test('verdictLabel covers every state the tree uses', () => {
  for (const state of ['', 'running', 'pass', 'fail']) assertTrue(state in catalog.verdictLabel, state);
  assertFalse('unknown' in catalog.verdictLabel);
});
