// --- repeat and until-failure, as settings ----------------------------

// What the passes control and the stop box ask of the run:
// { settings, passes, error }. `passes` is how many times the selection will
// be run, for the progress count: 1 when nothing was asked for, and null when
// the run is unbounded.
//
// An empty count is no --repeat, like every control left alone. With the stop
// box ticked that is --until-failure alone, which run_scripts takes as "until
// something breaks, however long" -- a run this page can end with Stop.
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
    count = null;
  }

  if (stopOnFailure) settings.push({ flag: '--until-failure', value: '', present: true });
  return { settings, passes: count, error: null };
}

// The tests a run is expected to finish, for "n/expected": an upper bound
// when stopping at the first failure, since the run may end sooner, and null
// for an unbounded run, which has no expectation to count against.
export function expectedTests(selected, passes) {
  return passes === null ? null : selected * passes;
}
