// --- how often to run, as settings ------------------------------------

export const ONCE = 'once';
export const TIMES = 'times';
export const UNTIL_FAILURE = 'until';

// What the "Run" choice and its count ask of the run: { settings, passes, error }.
// `passes` is how many times the selection will be run, for the progress count:
// 1 when nothing was asked for, and null when the run is unbounded.
//
//   once              no flag, like every control left alone
//   a number of times --repeat=N, N required
//   until a test fails
//                     --until-failure, with --repeat=N as an optional ceiling
//                     ("at most N passes"). With no count that is run_scripts'
//                     own unbounded form -- a run the page ends with Stop.
export function repeatSettings({ mode, count }) {
  if (mode !== TIMES && mode !== UNTIL_FAILURE) return { settings: [], passes: 1, error: null };

  const text = count.trim();
  const settings = [];
  let passes = null;

  if (text) {
    if (!/^\d+$/.test(text) || Number(text) < 1) {
      return { settings: [], passes: 1, error: 'The number of times must be a whole number, 1 or more.' };
    }
    passes = Number(text);
    settings.push({ flag: '--repeat', value: String(passes), present: true });
  } else if (mode === TIMES) {
    return { settings: [], passes: 1, error: 'How many times? Enter a number first.' };
  }

  if (mode === UNTIL_FAILURE) settings.push({ flag: '--until-failure', value: '', present: true });
  return { settings, passes, error: null };
}

// What the count field is called, and whether it is shown at all.
export function countField(mode) {
  if (mode === TIMES) return { label: 'Times', placeholder: '10', shown: true };
  if (mode === UNTIL_FAILURE) return { label: 'At most (optional)', placeholder: 'no limit', shown: true };
  return { label: 'Times', placeholder: '', shown: false };
}

// The tests a run is expected to finish, for "n/expected": an upper bound
// when stopping at the first failure, since the run may end sooner, and null
// for an unbounded run, which has no expectation to count against.
export function expectedTests(selected, passes) {
  return passes === null ? null : selected * passes;
}
