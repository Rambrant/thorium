// --- saving a run's log, as values ------------------------------------

// The file name a GET /api/log/ response would save under, from its
// Content-Disposition; the kind itself when there is none to read.
export function logFileName(disposition, kind) {
  const name = /filename="([^"]*)"/.exec(disposition || '');
  return name ? name[1] : kind;
}

// Saving is offered once the run is over -- the SARIF document is only
// written at runEnd -- and only for a log the run said it wrote: --no-logs
// and --skeleton runs write none. `savable` maps a kind to its file name.
export function saveLogState(savable, kind, running) {
  const available = !running && Boolean(savable[kind]);
  return {
    disabled: !available,
    title: available ? savable[kind] : (running ? 'Available when the run has finished' : 'The last run wrote no such log'),
  };
}
