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

// Drawn in batches, and only the last kMaxRows of them. A soak run is
// hundreds of thousands of rows: adding each as it arrived, and asking the
// pane how tall it had become after every one, made the browser lay the whole
// table out once per row, and nothing was ever let go -- a page that stopped
// answering, with the Stop button on it. What is dropped is counted, failures
// apart, and the saved log has all of it.
const kMaxRows = 20000;
const kFlushMs = 50;

let pending = [];
let timer = null;
let dropped = { rows: 0, failures: 0 };
const notice = el('tr', { className: 'section notice', hidden: true }, el('td', { colSpan: 4 }));

function showDropped() {
  notice.hidden = dropped.rows === 0;
  notice.firstChild.textContent = dropped.failures
    ? `${dropped.rows} earlier rows are not shown, ${dropped.failures} of them failures -- the saved log has every one`
    : `${dropped.rows} earlier rows are not shown -- the saved log has every one`;
  notice.classList.toggle('has-failure', dropped.failures > 0);
}

function rowElement(row) {
  if (row.section !== undefined) {
    const tr = el('tr', { className: 'section level' + row.level }, el('td', { colSpan: 4, textContent: row.section }));
    headings = headings.slice(0, row.level).concat(tr);
    return tr;
  }
  const tr = el('tr', { className: row.className });
  if (row.className === 'fail' || row.className === 'error') headings.forEach((h) => h.classList.add('has-failure'));
  if (row.tooltip) tr.title = row.tooltip;
  for (const [text, cls] of row.cells) {
    const cell = el('td', { textContent: text || '', className: cls || '' });
    if (cls === 'detail' && text) cell.title = text;
    tr.append(cell);
  }
  counts = tally(counts, row);
  return tr;
}

// Follows the run only while the operator is looking at its end, so
// scrolling back to read a failure is not yanked away by the next reading.
function flush() {
  timer = null;
  if (!pending.length) return;
  const follow = resultsBox.scrollTop + resultsBox.clientHeight >= resultsBox.scrollHeight - 30;
  const fragment = document.createDocumentFragment();
  for (const row of pending) fragment.append(rowElement(row));
  pending = [];
  rows.append(fragment);

  // The notice is the first row and stays; the oldest after it go.
  while (rows.children.length - 1 > kMaxRows) {
    const oldest = notice.nextSibling;
    dropped.rows += 1;
    if (oldest.classList.contains('fail') || oldest.classList.contains('error')) dropped.failures += 1;
    oldest.remove();
  }
  if (dropped.rows) showDropped();

  $('resultCount').textContent = countText(counts);
  if (follow) resultsBox.scrollTop = resultsBox.scrollHeight;
}

function addRow(row) {
  pending.push(row);
  if (timer === null) timer = setTimeout(flush, kFlushMs);
}

export function clearResults() {
  pending = [];
  rows.replaceChildren(notice);
  headings = [];
  counts = { checks: 0, failed: 0 };
  dropped = { rows: 0, failures: 0 };
  showDropped();
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

// The notice row is part of the table from the start.
clearResults();
