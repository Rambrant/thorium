// --- the results table ------------------------------------------------
//
// The old console's results list: what was read and what was concluded,
// in the human log's columns and order (see core/src/journal/report.cpp),
// so an operator who has read one recognises the other. Coloured only where
// there is a verdict -- an unset "passed" is not false, it is an event with
// no pass/fail notion at all, and painting it is how an Apply would come to
// look like a check that succeeded (see core::JournalRecord::Passed). Every
// Connect and Apply is still in Raw events, and in the SARIF log.

import { $, el, plural } from './dom.js';
import { state } from './state.js';

const log = $('log');
const resultsBox = $('results');
const rows = $('rows');
const counts = { checks: 0, failed: 0 };

// Follows the run only while the operator is looking at its end, so
// scrolling back to read a failure is not yanked away by the next reading.
export function addRow(className, cells, tooltip) {
  const follow = resultsBox.scrollTop + resultsBox.clientHeight >= resultsBox.scrollHeight - 30;
  const row = el('tr', { className });
  if (tooltip) row.title = tooltip;
  for (const [text, cls] of cells) {
    const cell = el('td', { textContent: text || '', className: cls || '' });
    if (cls === 'detail' && text) cell.title = text;
    row.append(cell);
  }
  rows.append(row);
  if (follow) resultsBox.scrollTop = resultsBox.scrollHeight;
}

function addSection(text) {
  const row = el('tr', { className: 'section' }, el('td', { colSpan: 5, textContent: text }));
  rows.append(row);
}

function updateResultCount() {
  $('resultCount').textContent = counts.checks
    ? plural(counts.checks, 'check') + (counts.failed ? ', ' + counts.failed + ' failed' : '')
    : '';
}

export function clearResults() {
  rows.replaceChildren();
  counts.checks = 0;
  counts.failed = 0;
  updateResultCount();
}

// The rows one event contributes, if any.
export function resultRows(e) {
  const run = state.run;
  switch (e.kind) {
    case 'groupStart':
      addSection(e.group + (e.description ? ' -- ' + e.description : ''));
      break;
    case 'phaseStart':
      // A hook's own bracket, shown because a run that fails in its setup
      // never reaches a test and would otherwise leave the table empty.
      run.current = e.group ? e.group + ' ' + e.phase : e.phase;
      break;
    case 'testStart':
      run.current = e.test;
      break;
    case 'event':
      if (e.verb === 'Verify') {
        const passed = e.passed === true;
        counts.checks += 1;
        if (!passed) counts.failed += 1;
        updateResultCount();
        const subject = e.subjectGroup ? e.subjectGroup + '::' + e.subject : (e.subject || e.detail);
        addRow(passed ? 'pass' : 'fail', [
          [run.current, 'tid'], [subject, 'subject'], [e.value, 'num'], [passed ? 'PASS' : 'FAIL', 'verdict'],
          [e.criterionText || e.detail, 'detail'],
        ], e.subject && e.detail ? e.detail : '');
      } else if (e.verb === 'Measure' || e.verb === 'Read' || e.verb === 'Fetch') {
        // The observation verbs only, as the human log does. Value alone,
        // never Value plus Unit: Value is already the printable form with
        // the unit in it ("0 V V" is what appending one produced).
        addRow('', [[run.current, 'tid'], [e.subject, 'subject'], [e.value, 'num'], [''], [e.detail, 'detail']]);
      }
      break;
  }
}

// The pane's two tabs: this table, and raw.js's log.
$('showResults').addEventListener('click', () => {
  resultsBox.hidden = false; log.hidden = true;
  $('failuresOnly').parentElement.hidden = false; $('rawFormat').hidden = true;
  $('showResults').classList.add('active'); $('showRaw').classList.remove('active');
});
$('showRaw').addEventListener('click', () => {
  resultsBox.hidden = true; log.hidden = false;
  $('failuresOnly').parentElement.hidden = true; $('rawFormat').hidden = false;
  $('showRaw').classList.add('active'); $('showResults').classList.remove('active');
  log.scrollTop = log.scrollHeight;
});
$('failuresOnly').addEventListener('change', () => {
  resultsBox.classList.toggle('failures-only', $('failuresOnly').checked);
});
