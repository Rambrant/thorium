// --- saving a run's log -----------------------------------------------

import { $, el } from './dom.js';
import { state } from './state.js';

// Which of the last run's logs can be saved: kind -> what the button's
// tooltip names it by. Filled from runStart for a run this page watched, and
// by asking the server on load for one it did not -- a reload, or a second
// window, must not lose the last run's logs just because the page forgot them.
let savableLogs = {};

export function setSavableLogs(logs) {
  savableLogs = logs;
  updateSaveLog();
}

// Saving is offered once the run is over -- the SARIF document is only
// written at runEnd -- and only for a log the run said it wrote: --no-logs
// and --skeleton runs write none.
export function updateSaveLog() {
  const kind = $('logKind').value;
  const available = !state.running && savableLogs[kind];
  $('saveLog').disabled = !available;
  $('saveLog').title = available ? savableLogs[kind] : (state.running ? 'Available when the run has finished' : 'The last run wrote no such log');
}

// HEAD rather than GET: the answer is in the status -- 200 there is one, 404
// the last run wrote none, 409 a run is still going -- and the file name in
// Content-Disposition; the file itself is not wanted until Save is pressed.
export async function probeSavableLogs() {
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
  if (!state.run) setSavableLogs(found);
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
