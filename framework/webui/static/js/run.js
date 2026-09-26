// --- a run ------------------------------------------------------------
//
// Starting one, and feeding its stream to the rest of the page. What the
// stream means for the run is model/run.js's.

import { $, setStatus } from './dom.js';
import { state } from './state.js';
import { plural } from './model/format.js';
import { applyEvent, newRun, outcome, progressText } from './model/run.js';
import { append, clearRaw } from './raw.js';
import { addStderr, clearResults, resultRows } from './results.js';
import { catalog, clearVerdicts, selection, setVerdict, updateSelection } from './catalog.js';
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
  const change = applyEvent(state.run, parsed);
  if (change) setVerdict(change.test, change.state);
  if (parsed.kind === 'runStart') {
    setSavableLogs(parsed.logs || {});
    showRunInfo(parsed.info || {});
  }
  if (parsed.kind === 'testEnd') setStatus(progressText(state.run));
  resultRows(parsed);
}

function runFinished() {
  setRunning(false);
  const { text, tone } = outcome(state.run);
  setStatus(text, tone);
}

function watchEvents() {
  if (currentSource) currentSource.close();
  currentSource = new EventSource('/api/events');
  currentSource.onmessage = (event) => {
    let parsed = null;
    try { parsed = JSON.parse(event.data); } catch (e) { /* not JSON -- show it raw */ }
    if (parsed && parsed.kind === 'stderr') {
      state.run.errors.push(parsed.text);
      addStderr(parsed.text);
      append({ stderr: parsed.text });
      return;
    }
    if (parsed) onEvent(parsed);
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
  clearVerdicts();
  setSavableLogs({});
  state.run = newRun(request.selection.length || catalog.length);

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
  setStatus('Running ' + plural(state.run.expected, 'test') + '...');
  watchEvents();
});

$('safe').addEventListener('click', async () => {
  setStatus('Safing the rig...');
  const response = await fetch('/safe', { method: 'POST' });
  const body = await response.json();
  if (body.ok) setStatus('Rig safed: all outputs off, all relays open.');
  else setStatus('SAFING FAILED -- do not approach the fixture', 'fail');
});
