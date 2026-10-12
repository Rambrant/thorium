// --- repeat and until-failure, as settings ----------------------------

// What the passes control and the stop box ask of the run:
// { settings, passes, error }. `passes` is how many times the selection will
// be run, for the progress count: 1 when nothing was asked for.
//
// An empty count is no --repeat, like every control left alone. Stopping at
// the first failure needs a count: run_scripts would take --until-failure
// alone as "until something breaks, however long", and this page has no way
// to stop a run, so it is never started unbounded from here -- it is an
// error, not a silent default.
export function repeatSettings({ passes, stopOnFailure }) {
  const text = passes.trim();
  const settings = [];
  let count = 1;

  if (text) {
    if (!/^\d+$/.test(text) || Number(text) < 1) {
      return { settings: [], passes: 1, error: 'Passes must be a whole number, 1 or more.' };
    }
    count = Number(text);
    settings.push({ flag: '--repeat', value: String(count), present: true });
  } else if (stopOnFailure) {
    return { settings: [], passes: 1, error: 'Stopping at the first failure needs a most-passes count. Enter one first.' };
  }

  if (stopOnFailure) settings.push({ flag: '--until-failure', value: '', present: true });
  return { settings, passes: count, error: null };
}

// The tests a run is expected to finish, for "n/expected". An upper bound
// when stopping at the first failure, since the run may end sooner.
export function expectedTests(selected, passes) {
  return selected * passes;
}
