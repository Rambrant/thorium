#pragma once

namespace webui
{
    //
    // The entire browser side of this program: one file, embedded rather than
    // read from disk. It carries the two things the old wxWidgets console had
    // on its face and the first version of this page did not:
    //
    //   - the header. DUT serial, operator and criteria before a run, because
    //     they are what makes the resulting report traceable to a unit and a
    //     person; and the run's own traceability header (runStart's "info")
    //     once it has started, because that is what the logs will say, not
    //     what the fields said.
    //   - the catalog, as a two-level tree with checkboxes. Ticking a group
    //     ticks its tests; groups start collapsed and carry their tests'
    //     verdict, so hundreds of tests still fit on one screen.
    //
    //   - the results list, in the human log's columns and colours: each
    //     Verify green or red, each reading plain, stderr as a red ERROR.
    //     The raw event stream is one tab away.
    //
    // A finished run's RTF or SARIF log can be saved from the bar (see
    // GET /api/log/ in main.cpp). Still not here: the generated options
    // form README.md's "Generating a form from --describe-options" describes.
    //
    // Embedded so the binary has no path to get wrong: wherever
    // thorium_webui.exe ends up -- built in place, copied to a bench,
    // launched by framework/launcher from a shortcut -- this comes with it.
    // set_mount_point() and a real static/ directory are the obvious next
    // step once there is more than one file to serve.
    //
    inline constexpr const char *  kIndexHtml = R"HTML(<!DOCTYPE html>
<html lang="en" translate="no">
<head>
<meta charset="utf-8">
<meta name="google" content="notranslate">
<title>Thorium Bench Console</title>
<style>
  :root { --bg: #1e1e1e; --panel: #252526; --line: #3a3a3a; --text: #ddd; --quiet: #8a8a8a;
          --pass: #4ec27a; --fail: #f05a5a; --run: #e0b84c; }
  * { box-sizing: border-box; }
  body { font-family: system-ui, sans-serif; margin: 0; padding: 1rem 1.25rem; background: var(--bg); color: var(--text);
         height: 100vh; display: flex; flex-direction: column; gap: 0.75rem; }
  h1 { font-size: 1.2rem; margin: 0; }
  button { font-size: 0.95rem; padding: 0.45rem 0.9rem; cursor: pointer; border-radius: 4px;
           border: 1px solid var(--line); background: #333; color: var(--text); }
  button:disabled { opacity: 0.45; cursor: default; }
  button.small { font-size: 0.8rem; padding: 0.2rem 0.55rem; }
  #safe { background: #7a1f1f; color: white; border: none; }
  #run { background: #1f5c3a; color: white; border: none; }
  input[type=text], select { font: inherit; padding: 0.35rem 0.5rem; background: #111; color: var(--text);
                             border: 1px solid var(--line); border-radius: 4px; width: 100%; }
  input:disabled, select:disabled { opacity: 0.6; }

  /* --- the header ---------------------------------------------------- */
  #setup { display: grid; grid-template-columns: auto 1fr auto 1fr auto 1fr; gap: 0.4rem 0.6rem; align-items: center; }
  #setup label { color: var(--quiet); font-size: 0.9rem; white-space: nowrap; }
  @media (max-width: 760px) { #setup { grid-template-columns: auto 1fr; } #panes { grid-template-columns: 1fr; } }
  .with-note { display: flex; gap: 0.5rem; align-items: center; }
  .with-note > input, .with-note > select { flex: 1; min-width: 7rem; }
  .with-note > .note { white-space: normal; flex: 0 1 auto; }
  .note { color: var(--quiet); font-size: 0.8rem; white-space: nowrap; }

  #runinfo { background: var(--panel); border: 1px solid var(--line); border-radius: 4px; padding: 0.5rem 0.75rem;
             display: flex; flex-wrap: wrap; gap: 0.3rem 1.5rem; font-size: 0.85rem; }
  #runinfo[hidden] { display: none; }
  #runinfo .k { color: var(--quiet); margin-right: 0.35rem; }
  #runinfo .warn { color: white; background: #7a1f1f; padding: 0 0.4rem; border-radius: 3px; font-weight: 600; }

  /* --- the two panes ------------------------------------------------- */
  #panes { flex: 1; min-height: 0; display: grid; grid-template-columns: minmax(18rem, 38%) 1fr; gap: 0.75rem; }
  .pane { background: var(--panel); border: 1px solid var(--line); border-radius: 4px; display: flex; flex-direction: column; min-height: 0; min-width: 0; }
  .pane-bar { display: flex; gap: 0.35rem; align-items: center; padding: 0.4rem 0.5rem; border-bottom: 1px solid var(--line); flex-wrap: wrap; }
  .pane-bar .note { margin-left: auto; }

  #tree { overflow-y: auto; padding: 0.25rem 0; flex: 1; }
  .group-row, .test-row { display: flex; align-items: center; gap: 0.35rem; padding: 0.15rem 0.5rem; }
  .group-row:hover, .test-row:hover { background: #2d2d30; }
  .group-row { font-weight: 600; }
  .caret { width: 1.4rem; height: 1.4rem; padding: 0; border: none; background: none; color: var(--quiet);
           font-size: 0.8rem; transition: transform 0.1s; }
  .group.collapsed .caret { transform: rotate(-90deg); }
  .group.collapsed .tests { display: none; }
  .tests { list-style: none; margin: 0; padding: 0 0 0 1.75rem; }
  .row-label { display: flex; align-items: baseline; gap: 0.5rem; flex: 1; min-width: 0; cursor: pointer; }
  .row-label input { align-self: center; }
  .count { color: var(--quiet); font-weight: normal; font-size: 0.8rem; }
  .tid { font-family: ui-monospace, monospace; font-size: 0.88rem; white-space: nowrap; }
  .desc { color: var(--quiet); font-size: 0.82rem; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .verdict { font-size: 0.75rem; font-weight: 700; min-width: 2.6rem; text-align: right; white-space: nowrap; }
  .verdict.pass { color: var(--pass); }
  .verdict.fail { color: var(--fail); }
  .verdict.running { color: var(--run); }

  #log { color: #9f9; font-family: ui-monospace, monospace; font-size: 0.8rem; padding: 0.75rem;
         overflow-y: auto; white-space: pre-wrap; overflow-wrap: anywhere; flex: 1; margin: 0; }
  #log[hidden], #results[hidden] { display: none; }

  /* --- the results table: the old console's columns and colours ---- */
  .tab.active { background: #4a4a4f; border-color: #6a6a70; }
  .pane-bar label.filter { color: var(--quiet); font-size: 0.8rem; display: flex; gap: 0.3rem; align-items: center; margin-left: 0.6rem; }
  #results { overflow: auto; flex: 1; }
  #results table { border-collapse: collapse; width: 100%; font-size: 0.82rem; }
  #results th { position: sticky; top: 0; background: var(--panel); text-align: left; font-weight: 600;
                color: var(--quiet); padding: 0.3rem 0.5rem; border-bottom: 1px solid var(--line); }
  #results td { padding: 0.15rem 0.5rem; border-bottom: 1px solid #2c2c2c; vertical-align: top; }
  #results td.num, #results th.num { text-align: right; font-family: ui-monospace, monospace; white-space: nowrap; }
  #results td.tid, #results td.subject { white-space: nowrap; }
  /* Detail takes what is left and is cut short rather than wrapped: a
     wrapped detail makes every row three lines tall. Hover for all of it. */
  #results td.detail { width: 100%; max-width: 0; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  #results td.verdict { text-align: left; }
  #results tr.pass td { color: var(--pass); }
  #results tr.fail td, #results tr.error td { color: var(--fail); }
  #results tr.error td.detail { white-space: pre-wrap; max-width: none; }
  #results tr.section td { color: var(--quiet); background: #2a2a2d; font-size: 0.78rem; padding-top: 0.3rem; }
  #results.failures-only tbody tr:not(.fail):not(.error) { display: none; }

  /* --- the bar ------------------------------------------------------- */
  #bar { display: flex; gap: 0.5rem; align-items: center; }
  #bar #safe { margin-left: 1.25rem; }
  #bar #logKind { width: auto; margin-left: 1.25rem; }
  #status { margin-left: auto; color: var(--quiet); }
  #status.pass { color: var(--pass); font-weight: 600; }
  #status.fail { color: var(--fail); font-weight: 600; }
</style>
</head>
<body>
  <h1>Thorium Bench Console</h1>

  <div id="setup">
    <label for="dutSerial">DUT serial</label>
    <input type="text" id="dutSerial" placeholder="SN-000123" autocomplete="off">
    <label for="operator">Operator</label>
    <input type="text" id="operator" placeholder="recorded in both logs" autocomplete="off">
    <label for="criteria">Criteria</label>
    <div class="with-note" id="criteriaCell">
      <input type="text" id="criteria" placeholder="the build's default" autocomplete="off">
      <span class="note" id="master"></span>
    </div>
  </div>

  <div id="runinfo" hidden></div>

  <div id="panes">
    <div class="pane">
      <div class="pane-bar">
        <button class="small" id="selAll">All</button>
        <button class="small" id="selNone">None</button>
        <button class="small" id="expandAll">Expand</button>
        <button class="small" id="collapseAll">Collapse</button>
        <span class="note" id="selCount"></span>
      </div>
      <div id="tree"><div class="note" style="padding:0.5rem">Loading the catalog...</div></div>
    </div>
    <div class="pane">
      <div class="pane-bar">
        <button class="small tab active" id="showResults">Results</button>
        <button class="small tab" id="showRaw">Raw events</button>
        <label class="filter"><input type="checkbox" id="failuresOnly"> Failures only</label>
        <span class="note" id="resultCount"></span>
      </div>
      <div id="results">
        <table>
          <thead><tr><th>Test</th><th>Subject</th><th class="num">Value</th><th>Verdict</th><th>Detail</th></tr></thead>
          <tbody id="rows"></tbody>
        </table>
      </div>
      <pre id="log" hidden></pre>
    </div>
  </div>

  <div id="bar">
    <button id="run" disabled>Run</button>
    <button id="safe">Safe the rig</button>
    <select id="logKind" title="Which of the run's two logs to save">
      <option value="rtf">RTF report</option>
      <option value="sarif">SARIF log</option>
    </select>
    <button id="saveLog" disabled title="Save the last run's log">Save log</button>
    <span id="status"></span>
  </div>

<script>
const $ = (id) => document.getElementById(id);
const log = $('log');
const tree = $('tree');
const statusEl = $('status');

// The catalog as the binary reports it, and the checkbox for each test id.
let catalog = [];
const testBoxes = new Map();   // id -> { box, verdict }
const groups = [];             // { el, box, ids }

// What the manifest says about criteria; null when there is no manifest.
let manifest = null;

const plural = (n, noun) => n + ' ' + noun + (n === 1 ? '' : 's');

function setStatus(text, tone) {
  statusEl.textContent = text;
  statusEl.className = tone || '';
}

function append(text) {
  log.textContent += text + "\n";
  log.scrollTop = log.scrollHeight;
}

// --- the results table ------------------------------------------------
//
// The old console's results list: what was read and what was concluded,
// in the human log's columns and order (see core/src/journal/report.cpp),
// so an operator who has read one recognises the other. Coloured only where
// there is a verdict -- an unset "passed" is not false, it is an event with
// no pass/fail notion at all, and painting it is how an Apply would come to
// look like a check that succeeded (see core::JournalRecord::Passed). Every
// Connect and Apply is still in Raw events, and in the SARIF log.

const resultsBox = $('results');
const rows = $('rows');
const counts = { checks: 0, failed: 0 };

// Follows the run only while the operator is looking at its end, so
// scrolling back to read a failure is not yanked away by the next reading.
function addRow(className, cells, tooltip) {
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

function clearResults() {
  rows.replaceChildren();
  counts.checks = 0;
  counts.failed = 0;
  updateResultCount();
}

// The rows one event contributes, if any.
function resultRows(e) {
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

$('showResults').addEventListener('click', () => {
  resultsBox.hidden = false; log.hidden = true;
  $('showResults').classList.add('active'); $('showRaw').classList.remove('active');
});
$('showRaw').addEventListener('click', () => {
  resultsBox.hidden = true; log.hidden = false;
  $('showRaw').classList.add('active'); $('showResults').classList.remove('active');
  log.scrollTop = log.scrollHeight;
});
$('failuresOnly').addEventListener('change', () => {
  resultsBox.classList.toggle('failures-only', $('failuresOnly').checked);
});

// --- the catalog ------------------------------------------------------

// "group|id|description", split on the first two pipes only -- a
// description may contain one (see webui::parseTestList).
function parseTestList(text) {
  const tests = [];
  for (const line of text.split('\n')) {
    const a = line.indexOf('|');
    const b = a < 0 ? -1 : line.indexOf('|', a + 1);
    if (b < 0) continue;
    tests.push({ group: line.slice(0, a), id: line.slice(a + 1, b), description: line.slice(b + 1).trim() });
  }
  return tests;
}

function el(tag, props, ...children) {
  const node = document.createElement(tag);
  Object.assign(node, props || {});
  for (const child of children) node.append(child);
  return node;
}

function buildTree(tests) {
  catalog = tests;
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

// Keeps each group's box in step with its tests: checked, unchecked, or the
// third state when only some are ticked.
function updateSelection() {
  for (const group of groups) {
    const ticked = group.tests.filter((t) => testBoxes.get(t.id).box.checked).length;
    group.box.checked = ticked === group.tests.length;
    group.box.indeterminate = ticked > 0 && ticked < group.tests.length;
  }
  const selected = selectedIds().length;
  $('selCount').textContent = selected + ' of ' + plural(catalog.length, 'test') + ' selected';
  $('run').disabled = running || catalog.length === 0 || selected === 0;
}

// A group's row summarises its tests' verdicts: running while any of them
// is, then the number that failed, or PASS once every test of it that ran
// passed. Blank for a group none of whose tests have run.
function refreshGroupVerdict(group) {
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
function selection() {
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

// --- the header -------------------------------------------------------

function applyManifest(doc) {
  const variants = (doc && doc.criteriaVariants) || [];
  if (variants.length === 0) return;   // keep the free-text field

  manifest = doc;
  const select = el('select', { id: 'criteria' });
  for (const variant of variants) select.append(el('option', { value: variant, textContent: variant }));
  select.value = variants.includes(doc.defaultCriteriaVariant) ? doc.defaultCriteriaVariant : variants[0];
  $('criteria').replaceWith(select);

  // The master, quiet and beside the picker rather than in it: not
  // selectable, but the applied variant alone does not say which table the
  // unchanged tolerances came from. See webui::Suite::MasterCriteria.
  if (doc.masterCriteriaVariant) $('master').textContent = 'unchanged rows from ' + doc.masterCriteriaVariant;
}

// The three header fields, as settings. A field left empty contributes no
// flag -- the operator field in particular, because run_scripts already
// falls back to $USER and a value this page filled in would be the page
// making a traceability claim on somebody's behalf.
function headerSettings() {
  const settings = [];
  const push = (flag, value) => { if (value) settings.push({ flag, value, present: true }); };
  push('--dut-serial', $('dutSerial').value.trim());
  push('--operator', $('operator').value.trim());

  // --criteria only when moved off the build's own default, so an operator
  // who never touched it follows the default the day it changes.
  const criteria = $('criteria').value.trim();
  if (!manifest || criteria !== manifest.defaultCriteriaVariant) push('--criteria', criteria);
  return settings;
}

function showRunInfo(info) {
  const box = $('runinfo');
  box.replaceChildren();
  const field = (key, value) => {
    if (!value) return;
    box.append(el('span', {}, el('span', { className: 'k', textContent: key }), value));
  };

  // Promoted above everything else, for the reason core/journal/journal.hpp
  // puts it in the header at all: it is the difference between watching a
  // DUT and watching a file.
  if (info.benchAttached === false) {
    box.append(el('span', { className: 'warn', textContent: 'NO BENCH ATTACHED -- readings are not from the rig' }));
  }
  field('DUT', [info.dutName, info.dutSerial].filter(Boolean).join(' / '));
  field('Rig', info.rigName);
  field('Criteria', info.criteriaVariant +
    (info.criteriaMaster && info.criteriaMaster !== info.criteriaVariant ? ' (unchanged rows from ' + info.criteriaMaster + ')' : ''));
  field('Operator', info.operator);
  field('Started', info.startedLocal);
  field('Suite', info.suiteVersion);
  if (Array.isArray(info.instruments) && info.instruments.length) {
    const inst = el('span', {}, el('span', { className: 'k', textContent: 'Instruments' }), plural(info.instruments.length, 'instrument'));
    inst.title = info.instruments.join('\n');
    box.append(inst);
  }
  box.hidden = false;
}

// --- a run ------------------------------------------------------------

let running = false;
let currentSource = null;
let run = null;

function setRunning(value) {
  running = value;
  for (const box of tree.querySelectorAll('input[type=checkbox]')) box.disabled = value;
  for (const id of ['dutSerial', 'operator', 'criteria', 'selAll', 'selNone']) $(id).disabled = value;
  // The Safe button is untouched, deliberately: it is live at every moment.
  updateSelection();
  updateSaveLog();
}

// Which of the last run's logs can be saved: kind -> what the button's
// tooltip names it by. Filled from runStart for a run this page watched, and
// by asking the server on load for one it did not -- a reload, or a second
// window, must not lose the last run's logs just because the page forgot them.
let savableLogs = {};

// Saving is offered once the run is over -- the SARIF document is only
// written at runEnd -- and only for a log the run said it wrote: --no-logs
// and --skeleton runs write none.
function updateSaveLog() {
  const kind = $('logKind').value;
  const available = !running && savableLogs[kind];
  $('saveLog').disabled = !available;
  $('saveLog').title = available ? savableLogs[kind] : (running ? 'Available when the run has finished' : 'The last run wrote no such log');
}

// HEAD rather than GET: the answer is in the status -- 200 there is one, 404
// the last run wrote none, 409 a run is still going -- and the file name in
// Content-Disposition; the file itself is not wanted until Save is pressed.
async function probeSavableLogs() {
  const found = {};
  for (const kind of ['rtf', 'sarif']) {
    try {
      const response = await fetch('/api/log/' + kind, { method: 'HEAD' });
      if (!response.ok) continue;
      const disposition = response.headers.get('Content-Disposition') || '';
      const name = /filename="([^"]*)"/.exec(disposition);
      found[kind] = name ? name[1] : kind;
    } catch (e) { /* the server is unreachable -- nothing to offer */ }
  }
  // A run started from this page while the probe was out has the newer answer.
  if (!run) { savableLogs = found; updateSaveLog(); }
}

$('logKind').addEventListener('change', updateSaveLog);

// The server picks the file from the run's own runStart line; the page
// only says which of the two it wants.
$('saveLog').addEventListener('click', () => {
  const link = el('a', { href: '/api/log/' + $('logKind').value, download: '' });
  document.body.append(link);
  link.click();
  link.remove();
});

function onEvent(parsed) {
  switch (parsed.kind) {
    case 'runStart':
      run.sawRunStart = true;
      savableLogs = parsed.logs || {};
      showRunInfo(parsed.info || {});
      break;
    case 'testStart': {
      const entry = testBoxes.get(parsed.test);
      if (entry) {
        entry.verdict.textContent = '...'; entry.verdict.className = 'verdict running';
        refreshGroupVerdict(entry.group);
      }
      break;
    }
    case 'testEnd': {
      run.finished += 1;
      if (!parsed.passed) run.failed += 1;
      const entry = testBoxes.get(parsed.test);
      if (entry) {
        entry.verdict.textContent = parsed.passed ? 'PASS' : 'FAIL';
        entry.verdict.className = 'verdict ' + (parsed.passed ? 'pass' : 'fail');
        refreshGroupVerdict(entry.group);
      }
      setStatus(run.finished + '/' + run.expected + ' -- ' + run.failed + ' failed');
      break;
    }
    case 'runEnd':
      run.passed = parsed.allPassed;
      break;
  }
}

// The four outcomes README.md's "What a run means" lists, told apart by the
// stream rather than by an exit code this page never sees.
function runFinished() {
  setRunning(false);
  if (!run.sawRunStart) {
    setStatus('The run did not start' + (run.errors.length ? ': ' + run.errors.join(' ') : ''), 'fail');
  } else if (run.passed === undefined) {
    setStatus('Run ended without reporting a result -- the rig state is unknown; safe the rig', 'fail');
  } else if (run.passed) {
    setStatus('PASSED -- ' + plural(run.finished, 'test'), 'pass');
  } else {
    setStatus('FAILED -- ' + run.failed + ' of ' + run.finished, 'fail');
  }
}

function watchEvents() {
  if (currentSource) currentSource.close();
  currentSource = new EventSource('/api/events');
  currentSource.onmessage = (event) => {
    let parsed = null;
    try { parsed = JSON.parse(event.data); } catch (e) { /* not JSON -- show it raw */ }
    if (parsed && parsed.kind === 'stderr') {
      // Shown in the table rather than swallowed: everything run_scripts
      // writes there is a reason a run did not happen, and in every such
      // case there are no events at all, so this is all the operator sees.
      run.errors.push(parsed.text);
      addRow('error', [[''], [''], [''], ['ERROR', 'verdict'], [parsed.text, 'detail']]);
      append('[stderr] ' + parsed.text);
      return;
    }
    if (parsed) { onEvent(parsed); resultRows(parsed); }
    append(event.data);
  };
  // The server ends the stream when the run ends; EventSource reports that
  // as an error and would reconnect, which would replay the run from the top.
  currentSource.onerror = () => { currentSource.close(); currentSource = null; runFinished(); };
}

$('run').addEventListener('click', async () => {
  const request = { selection: selection(), settings: headerSettings(), extra: [] };
  log.textContent = '';
  clearResults();
  $('runinfo').hidden = true;
  for (const { verdict } of testBoxes.values()) { verdict.textContent = ''; verdict.className = 'verdict'; }
  groups.forEach(refreshGroupVerdict);
  savableLogs = {};
  run = { sawRunStart: false, passed: undefined, finished: 0, failed: 0, errors: [], current: '',
          expected: request.selection.length || catalog.length };

  setRunning(true);
  setStatus('Starting run...');
  const response = await fetch('/api/run', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(request),
  });
  if (response.status === 409) {
    setRunning(false);
    setStatus('A run is already active.', 'fail');
    return;
  }
  setStatus('Running ' + plural(run.expected, 'test') + '...');
  watchEvents();
});

$('safe').addEventListener('click', async () => {
  setStatus('Safing the rig...');
  const response = await fetch('/safe', { method: 'POST' });
  const body = await response.json();
  if (body.ok) setStatus('Rig safed: all outputs off, all relays open.');
  else setStatus('SAFING FAILED -- do not approach the fixture', 'fail');
});

// --- start-up ---------------------------------------------------------

probeSavableLogs();

// Held open for as long as this page is: it is how the launcher knows a
// console window is still open (see /api/presence in main.cpp). Nothing is
// read from it; EventSource's own reconnect covers a server restart.
new EventSource('/api/presence');

fetch('/api/manifest').then((r) => (r.ok ? r.json() : null)).then(applyManifest).catch(() => {});

fetch('/api/tests')
  .then((r) => { if (!r.ok) throw new Error(); return r.text(); })
  .then((text) => buildTree(parseTestList(text)))
  .catch(() => {
    tree.replaceChildren(el('div', { className: 'note', textContent: 'Could not ask run_scripts for its catalog.' }));
    setStatus('Could not run --list-tests', 'fail');
  });
</script>
</body>
</html>
)HTML";
}
