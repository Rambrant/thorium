// --- record / replay, as settings -------------------------------------

export const LIVE = 'live';
export const RECORD = 'record';
export const REPLAY = 'replay';

// What the readings control asks of the run: { settings, error }. Live adds
// nothing -- the control left alone is no flag, like the header's fields. The
// two file modes need a path, and an empty one is an error rather than a
// silent live run: run_scripts refuses a bare --record= up front, but a run
// the operator believed was being recorded must not start at all.
//
// --record and --replay are exclusive in run_scripts, and a single mode
// choice cannot name both, so there is nothing for this to check there.
export function readingsSettings({ mode, path }) {
  if (mode !== RECORD && mode !== REPLAY) return { settings: [], error: null };
  const value = path.trim();
  if (!value) {
    return { settings: [], error: (mode === RECORD ? 'Record to' : 'Replay from') + ' which file? Enter a path first.' };
  }
  return { settings: [{ flag: '--' + mode, value, present: true }], error: null };
}
