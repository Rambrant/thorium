// --- a run ------------------------------------------------------------

import { $, plural, setStatus } from './dom.js';
import { state } from './state.js';
import { append, clearRaw } from './raw.js';
import { addRow, clearResults, resultRows } from './results.js';
import { catalog, groups, refreshGroupVerdict, selection, testBoxes, updateSelection } from './catalog.js';
import { headerSettings, showRunInfo } from './header.js';
import { setSavableLogs, updateSaveLog } from './logs.js';

let currentSource = null;

function setRunning(value) {
  state.running = value;
  for (const box of $('tree').querySelectorAll('input[type=checkbox]')) box.disabled = value;
  for (const id of ['dutSerial', 'operator', 'criteria', 'selAll', 'selNone']) $(id).disabled = value;
  // The Safe button is untouched, deliberately: it is live at every moment.
  updateSelection();
  updateSaveLog();
}

function onEvent(parsed) {
  const run = state.run;
  switch (parsed.kind) {
    case 'runStart':
      run.sawRunStart = true;
      setSavableLogs(parsed.logs || {});
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
  const run = state.run;
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
      state.run.errors.push(parsed.text);
      addRow('error', [[''], [''], [''], ['ERROR', 'verdict'], [parsed.text, 'detail']]);
      append({ stderr: parsed.text });
      return;
    }
    if (parsed) { onEvent(parsed); resultRows(parsed); }
    append({ data: event.data, parsed });
  };
  // The server ends the stream when the run ends; EventSource reports that
  // as an error and would reconnect, which would replay the run from the top.
  currentSource.onerror = () => { currentSource.close(); currentSource = null; runFinished(); };
}

$('run').addEventListener('click', async () => {
  const request = { selection: selection(), settings: headerSettings(), extra: [] };
  clearRaw();
  clearResults();
  $('runinfo').hidden = true;
  for (const { verdict } of testBoxes.values()) { verdict.textContent = ''; verdict.className = 'verdict'; }
  groups.forEach(refreshGroupVerdict);
  setSavableLogs({});
  const run = { sawRunStart: false, passed: undefined, finished: 0, failed: 0, errors: [], current: '',
                expected: request.selection.length || catalog.length };
  state.run = run;

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
