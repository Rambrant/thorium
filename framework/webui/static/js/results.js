// --- the results table ------------------------------------------------
//
// Draws the rows model/results.js decides on -- see there for what is shown
// and why.

import { $, el } from './dom.js';
import { state } from './state.js';
import { countText, rowsFor, stderrRow, tally } from './model/results.js';

const log = $('log');
const resultsBox = $('results');
const rows = $('rows');
let counts = { checks: 0, failed: 0 };
// The open group's and test's heading rows, by level: a failure under them
// marks them, so Failures only still says where each failure was.
let headings = [];

// Follows the run only while the operator is looking at its end, so
// scrolling back to read a failure is not yanked away by the next reading.
function addRow(row) {
  const follow = resultsBox.scrollTop + resultsBox.clientHeight >= resultsBox.scrollHeight - 30;
  if (row.section !== undefined) {
    const tr = el('tr', { className: 'section level' + row.level }, el('td', { colSpan: 4, textContent: row.section }));
    headings = headings.slice(0, row.level).concat(tr);
    rows.append(tr);
    if (follow) resultsBox.scrollTop = resultsBox.scrollHeight;
    return;
  }
  const tr = el('tr', { className: row.className });
  if (row.className === 'fail' || row.className === 'error') headings.forEach((h) => h.classList.add('has-failure'));
  if (row.tooltip) tr.title = row.tooltip;
  for (const [text, cls] of row.cells) {
    const cell = el('td', { textContent: text || '', className: cls || '' });
    if (cls === 'detail' && text) cell.title = text;
    tr.append(cell);
  }
  rows.append(tr);
  if (follow) resultsBox.scrollTop = resultsBox.scrollHeight;

  counts = tally(counts, row);
  $('resultCount').textContent = countText(counts);
}

export function clearResults() {
  rows.replaceChildren();
  headings = [];
  counts = { checks: 0, failed: 0 };
  $('resultCount').textContent = countText(counts);
}

export function resultRows(e) {
  const { current, rows } = rowsFor(e, state.run.current);
  state.run.current = current;
  rows.forEach(addRow);
}

export function addStderr(text) {
  addRow(stderrRow(text));
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
