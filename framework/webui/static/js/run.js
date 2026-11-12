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
  for (const id of ['dutSerial', 'operator', 'criteria', 'repeatMode', 'repeatCount', 'selAll', 'selNone']) $(id).disabled = value;
  // The Safe button is untouched, deliberately: it is live at every moment.
  // Emergency is the opposite: only a run gives it anything to do. And the
  // Run button is the Stop button while there is one (see updateRunButton).
  state.stopping = false;
  $('emergency').disabled = !value;
  updateRunButton();
  setReadingsDisabled(value);
  updateSelection();
  updateSaveLog();
}

function updateRunButton() {
  const button = $('run');
  button.textContent = !state.running ? 'Run' : state.stopping ? 'Stopping...' : 'Stop';
  button.classList.toggle('is-stop', state.running);
  button.title = state.running
    ? 'End the run after the current test: teardown runs, the rig is safed, the logs are written'
    : '';
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
  const { text, tone, detail } = outcome(state.run);
  setStatus(text, tone, detail);
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

// A page opened or reloaded while a run is going: join it.
//
// Without this the page comes up idle beside a live run -- Run answers "a run
// is already active", and Stop and Emergency stop, which only a running page
// offers, are not there for the one person who can see the rig. The stream
// replays a run from its first line to whoever opens it (see /api/events), so
// joining is the same watchEvents() a page that started the run does. What
// this page cannot know is how many tests the run was asked for, so the
// progress line counts up with no total.
//
// Called once the catalog tree is built (main.js), because the stream's
// verdicts land on its rows.
export async function attachToRun() {
  if (state.running) return;
  let status = null;
  try {
    status = await (await fetch('/api/status')).json();
  } catch (e) { return; }
  if (!status || !status.running || state.running) return;
  clearRaw();
  clearResults();
  clearVerdicts();
  state.run = newRun(null);
  setRunning(true);
  setStatus('A run is in progress...');
  watchEvents();
}

async function startRun() {
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
  let response = null;
  try {
    response = await fetch('/api/run', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(request),
    });
  } catch (e) {
    // The server did not answer, so nothing was started.
    setRunning(false);
    setStatus('Could not reach the console server -- no run was started.', 'fail');
    return;
  }
  if (response.status === 409) {
    setRunning(false);
    setStatus('A run is already active.', 'fail');
    return;
  }
  setStatus(runningText(state.run.expected));
  watchEvents();
}

$('safe').addEventListener('click', async () => {
  setStatus('Safing the rig...');
  // Anything but a reply saying ok is a rig nobody has confirmed idle: a
  // server that did not answer, or one that answered with something else.
  let body = { ok: false };
  try {
    body = await (await fetch('/safe', { method: 'POST' })).json();
  } catch (e) { /* ok stays false */ }
  if (body.ok) setStatus('Rig safed: all outputs off, all relays open.');
  else setStatus('SAFING FAILED -- do not approach the fixture', 'fail');
});

async function stopRun() {
  // Before the request, not after: the run may end before the reply does, and
  // the outcome it ends with must already know it was asked to.
  state.run.stopped = 'stop';
  state.stopping = true;
  updateRunButton();
  updateSelection();
  setStatus('Stopping after the current test...');
  const response = await fetch('/api/stop', { method: 'POST' });
  if (!response.ok) {
    // 409: the run ended between the click and the request.
    state.run.stopped = null;
    state.stopping = false;
    updateRunButton();
    updateSelection();
    if (state.running) setStatus('There was no run to stop.');
  }
}

// The one button: Run when idle, Stop while a run is going.
$('run').addEventListener('click', () => (state.running ? stopRun() : startRun()));

// No confirmation, on purpose: this is the button for when there is no time
// for one, and Stop is the calm way out.
$('emergency').addEventListener('click', async () => {
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
