// --- the results table, as rows ---------------------------------------
//
// The old console's results list: what was read and what was concluded,
// in the human log's columns and order (see core/src/journal/report.cpp),
// so an operator who has read one recognises the other. Coloured only where
// there is a verdict -- an unset "passed" is not false, it is an event with
// no pass/fail notion at all, and painting it is how an Apply would come to
// look like a check that succeeded (see core::JournalRecord::Passed). Every
// Connect and Apply is still in Raw events, and in the SARIF log.
//
// Laid out as a tree, the way the human log reads: a group's heading, each
// of its tests -- or a hook's bracket -- as a heading under it, and the
// readings and checks under that. A row is { className, cells: [[text,
// cls], ...], tooltip }, or { section, level } for a heading, level 0 a
// group and 1 a test or hook. The view draws them; nothing here knows how.

import { plural } from './format.js';

// What one event contributes. `current` is the running test, or a hook's
// bracket, and is handed back, changed or not: the view keeps it so a
// check's tooltip can still name its test when Failures only hides the
// headings around it.
export function rowsFor(e, current) {
  const rows = [];
  switch (e.kind) {
    case 'groupStart':
      rows.push({ section: e.group + (e.description ? ' -- ' + e.description : ''), level: 0 });
      break;
    case 'phaseStart':
      // A hook's own bracket, shown because a run that fails in its setup
      // never reaches a test and would otherwise leave the table empty.
      current = e.group ? e.group + ' ' + e.phase : e.phase;
      rows.push({ section: current, level: 1 });
      break;
    case 'testStart':
      current = e.test;
      rows.push({ section: e.test + (e.description ? ' -- ' + e.description : ''), level: 1 });
      break;
    case 'event':
      if (e.verb === 'Verify') {
        const passed = e.passed === true;
        const subject = e.subjectGroup ? e.subjectGroup + '::' + e.subject : (e.subject || e.detail);
        rows.push({
          className: passed ? 'pass' : 'fail',
          cells: [[subject, 'subject'], [e.value, 'num'], [passed ? 'PASS' : 'FAIL', 'verdict'],
                  [e.criterionText || e.detail, 'detail']],
          tooltip: [current, e.subject && e.detail].filter(Boolean).join(': '),
        });
      } else if (e.verb === 'Measure' || e.verb === 'Read' || e.verb === 'Fetch') {
        // The observation verbs only, as the human log does. Value alone,
        // never Value plus Unit: Value is already the printable form with
        // the unit in it ("0 V V" is what appending one produced).
        rows.push({
          className: '',
          cells: [[e.subject, 'subject'], [e.value, 'num'], [''], [e.detail, 'detail']],
          tooltip: current || '',
        });
      }
      break;
  }
  return { current, rows };
}

// Shown in the table rather than swallowed: everything run_scripts writes to
// stderr is a reason a run did not happen, and in every such case there are
// no events at all, so this is all the operator sees.
export function stderrRow(text) {
  return { className: 'error', cells: [[''], [''], ['ERROR', 'verdict'], [text, 'detail']], tooltip: '' };
}

// Counts checks -- Verify rows -- and not readings or stderr.
export function tally(counts, row) {
  if (row.className !== 'pass' && row.className !== 'fail') return counts;
  return { checks: counts.checks + 1, failed: counts.failed + (row.className === 'fail' ? 1 : 0) };
}

export function countText(counts) {
  return counts.checks
    ? plural(counts.checks, 'check') + (counts.failed ? ', ' + counts.failed + ' failed' : '')
    : '';
}
