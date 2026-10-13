// --- a run ------------------------------------------------------------
//
// Starting one, and feeding its stream to the rest of the page. What the
// stream means for the run is model/run.js's.

import { $, setStatus } from './dom.js';
import { state } from './state.js';
import { applyEvent, emergencyOutcome, newRun, outcome, progressText, runningText } from './model/run.js';
import { append, clearRaw } from './raw.js';
import { addStderr, clearResults, resultRows } from './results.js';
import { catalog, clearVerdicts, selection, setVerdict, updateSelection } from './catalog.js';
import { headerSettings, showRunInfo } from './header.js';
import { setSavableLogs, updateSaveLog } from './logs.js';
import { readingsSettings, setReadingsDisabled } from './readings.js';
import { repeatSettings } from './repeat.js';
import { expectedTests } from './model/repeat.js';

let currentSource = null;

function setRunning(value) {
  state.running = value;
  for (const box of $('tree').querySelectorAll('input[type=checkbox]')) box.disabled = value;
  for (const id of ['dutSerial', 'operator', 'criteria', 'passes', 'stopOnFailure', 'selAll', 'selNone']) $(id).disabled = value;
  // The Safe button is untouched, deliberately: it is live at every moment.
  // Stop and Emergency are the opposite: only a run gives them anything to do.
  $('stop').disabled = !value;
  $('emergency').disabled = !value;
  setReadingsDisabled(value);
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
  // The emergency button's own reply says how it went, and may already have.
  if (state.run.stopped === 'emergency') return;
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
  const readings = readingsSettings();
  if (readings.error) {
    setStatus(readings.error, 'fail');
    return;
  }
  const repeat = repeatSettings();
  if (repeat.error) {
    setStatus(repeat.error, 'fail');
    return;
  }
  const request = {
    selection: selection(),
    settings: [...headerSettings(), ...readings.settings, ...repeat.settings],
    extra: [],
  };
  clearRaw();
  clearResults();
  $('runinfo').hidden = true;
  clearVerdicts();
  setSavableLogs({});
  state.run = newRun(expectedTests(request.selection.length || catalog.length, repeat.passes));

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
  setStatus(runningText(state.run.expected));
  watchEvents();
});

$('safe').addEventListener('click', async () => {
  setStatus('Safing the rig...');
  const response = await fetch('/safe', { method: 'POST' });
  const body = await response.json();
  if (body.ok) setStatus('Rig safed: all outputs off, all relays open.');
  else setStatus('SAFING FAILED -- do not approach the fixture', 'fail');
});

$('stop').addEventListener('click', async () => {
  $('stop').disabled = true;
  // Before the request, not after: the run may end before the reply does, and
  // the outcome it ends with must already know it was asked to.
  state.run.stopped = 'stop';
  setStatus('Stopping after the current test...');
  const response = await fetch('/api/stop', { method: 'POST' });
  if (!response.ok) {
    // 409: the run ended between the click and the request.
    state.run.stopped = null;
    if (state.running) setStatus('There was no run to stop.');
  }
});

// No confirmation, on purpose: this is the button for when there is no time
// for one, and Stop is the calm way out.
$('emergency').addEventListener('click', async () => {
  $('stop').disabled = true;
  $('emergency').disabled = true;
  state.run.stopped = 'emergency';
  setStatus('EMERGENCY STOP -- killing the run and safing the rig...', 'fail');
  let reply = { killed: false, safed: false };
  try {
    reply = await (await fetch('/api/emergency', { method: 'POST' })).json();
  } catch (e) { /* the server did not answer: safed stays false */ }
  const { text, tone } = emergencyOutcome(reply);
  setStatus(text, tone);
});
