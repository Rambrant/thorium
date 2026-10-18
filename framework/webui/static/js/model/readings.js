// --- record / replay, as settings -------------------------------------

export const LIVE = 'live';
export const REPLAY = 'replay';

// What the readings controls ask of the run: { settings, error }.
//
// Two controls, not one: where the readings come from (the bench, or a
// recording), and whether to save them. Saving only makes sense for the bench:
// run_scripts refuses --record with --replay, because recording a replay would
// write out the values it had just been fed as a file that looks captured. So
// the checkbox is ignored under replay -- the view disables it, and this does
// not rely on that.
//
// Left alone, the controls add nothing, like the header's fields. A file mode
// with no path is an error rather than a silent live run: a run the operator
// believed was being recorded, or replayed, must not start at all.
export function readingsSettings({ source, record, path }) {
  const flag = source === REPLAY ? '--replay' : record ? '--record' : null;
  if (!flag) return { settings: [], error: null };

  const value = path.trim();
  if (!value) {
    return { settings: [], error: (source === REPLAY ? 'Replay from' : 'Record to') + ' which file? Enter a path first.' };
  }
  return { settings: [{ flag, value, present: true }], error: null };
}

// What the file field is called, and whether it is shown at all.
export function pathField({ source, record }) {
  if (source === REPLAY) return { label: 'Replay from', shown: true };
  return record ? { label: 'Record to', shown: true } : { label: 'File', shown: false };
}
