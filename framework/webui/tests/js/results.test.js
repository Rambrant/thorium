import { test, assertEqual } from './harness.js';
import { countText, rowsFor, stderrRow, tally } from '../../static/js/model/results.js';

test('rowsFor: a group heading, with its description when there is one', () => {
  assertEqual(rowsFor({ kind: 'groupStart', group: 'Power', description: 'rails' }, '').rows, [{ section: 'Power -- rails', level: 0 }]);
  assertEqual(rowsFor({ kind: 'groupStart', group: 'Power' }, '').rows, [{ section: 'Power', level: 0 }]);
});

test('rowsFor: testStart and phaseStart change the current test, and head it one level in', () => {
  assertEqual(rowsFor({ kind: 'testStart', test: 'P1' }, 'old'), { current: 'P1', rows: [{ section: 'P1', level: 1 }] });
  assertEqual(rowsFor({ kind: 'testStart', test: 'P1', description: '3V3 rail' }, 'old').rows, [{ section: 'P1 -- 3V3 rail', level: 1 }]);
  assertEqual(rowsFor({ kind: 'phaseStart', group: 'Power', phase: 'setUp' }, 'old'),
              { current: 'Power setUp', rows: [{ section: 'Power setUp', level: 1 }] });
  assertEqual(rowsFor({ kind: 'phaseStart', phase: 'suiteSetUp' }, 'old').current, 'suiteSetUp');
});

test('rowsFor: a passing Verify', () => {
  const { current, rows } = rowsFor({
    kind: 'event', verb: 'Verify', passed: true, subject: 'Vout', value: '3.30 V',
    criterionText: '3.2 V <= Vout <= 3.4 V', detail: 'within limits',
  }, 'P1');
  assertEqual(current, 'P1');
  assertEqual(rows, [{
    className: 'pass',
    cells: [['Vout', 'subject'], ['3.30 V', 'num'], ['PASS', 'verdict'], ['3.2 V <= Vout <= 3.4 V', 'detail']],
    tooltip: 'P1: within limits',
  }]);
});

test('rowsFor: Verify without passed:true is a FAIL', () => {
  const row = rowsFor({ kind: 'event', verb: 'Verify', subject: 'Vout', detail: 'd' }, 'P1').rows[0];
  assertEqual(row.className, 'fail');
  assertEqual(row.cells[2], ['FAIL', 'verdict']);
});

test('rowsFor: a Verify subject is qualified by its group, or falls back to the detail', () => {
  const grouped = rowsFor({ kind: 'event', verb: 'Verify', passed: true, subjectGroup: 'Rails', subject: 'Vout' }, '').rows[0];
  assertEqual(grouped.cells[0], ['Rails::Vout', 'subject']);
  const bare = rowsFor({ kind: 'event', verb: 'Verify', passed: true, detail: 'no subject' }, '').rows[0];
  assertEqual(bare.cells[0], ['no subject', 'subject']);
  assertEqual(bare.tooltip, '');
});

test('rowsFor: readings get an uncoloured row, value without its unit appended', () => {
  for (const verb of ['Measure', 'Read', 'Fetch']) {
    const rows = rowsFor({ kind: 'event', verb, subject: 'Vout', value: '0 V', unit: 'V', detail: 'dc' }, 'P1').rows;
    assertEqual(rows, [{
      className: '', cells: [['Vout', 'subject'], ['0 V', 'num'], [''], ['dc', 'detail']], tooltip: 'P1',
    }], verb);
  }
});

test('rowsFor: Connect, Apply and other events add no row', () => {
  for (const e of [{ kind: 'event', verb: 'Connect' }, { kind: 'event', verb: 'Apply' }, { kind: 'runEnd' }]) {
    assertEqual(rowsFor(e, 'P1'), { current: 'P1', rows: [] }, JSON.stringify(e));
  }
});

test('tally counts checks, not readings or stderr', () => {
  let counts = { checks: 0, failed: 0 };
  for (const className of ['pass', 'fail', '', 'error', 'pass']) counts = tally(counts, { className });
  assertEqual(counts, { checks: 3, failed: 1 });
  const section = { checks: 3, failed: 1 };
  assertEqual(tally(section, { section: 'G', level: 0 }), section);
});

test('countText', () => {
  assertEqual(countText({ checks: 0, failed: 0 }), '');
  assertEqual(countText({ checks: 1, failed: 0 }), '1 check');
  assertEqual(countText({ checks: 4, failed: 2 }), '4 checks, 2 failed');
});

test('stderrRow: an ERROR row with the text as its detail', () => {
  assertEqual(stderrRow('no such test: X'), {
    className: 'error', cells: [[''], [''], ['ERROR', 'verdict'], ['no such test: X', 'detail']], tooltip: '',
  });
});
