import { test, assertEqual, assertTrue } from './harness.js';
import { applyEvent, newRun, outcome, progressText } from '../../static/js/model/run.js';

function play(events, expected = 2) {
  const run = newRun(expected);
  const changes = events.map((e) => applyEvent(run, e));
  return { run, changes };
}

test('applyEvent: tests go running, then pass or fail', () => {
  const { run, changes } = play([
    { kind: 'runStart' },
    { kind: 'testStart', test: 'T1' }, { kind: 'testEnd', test: 'T1', passed: true },
    { kind: 'testStart', test: 'T2' }, { kind: 'testEnd', test: 'T2', passed: false },
    { kind: 'runEnd', allPassed: false },
  ]);
  assertEqual(changes, [
    null,
    { test: 'T1', state: 'running' }, { test: 'T1', state: 'pass' },
    { test: 'T2', state: 'running' }, { test: 'T2', state: 'fail' },
    null,
  ]);
  assertTrue(run.sawRunStart);
  assertEqual([run.finished, run.failed, run.passed], [2, 1, false]);
});

test('progressText', () => {
  const { run } = play([{ kind: 'runStart' }, { kind: 'testEnd', test: 'T1', passed: false }], 3);
  assertEqual(progressText(run), '1/3 -- 1 failed');
});

// README.md's "What a run means": four outcomes, told apart by the stream.
test('outcome: no runStart -- the run did not start, with its stderr', () => {
  const run = newRun(1);
  assertEqual(outcome(run), { text: 'The run did not start', tone: 'fail' });
  run.errors.push('unknown test: X', 'see --list-tests');
  assertEqual(outcome(run).text, 'The run did not start: unknown test: X see --list-tests');
});

test('outcome: runStart but no runEnd -- the rig state is unknown', () => {
  const { run } = play([{ kind: 'runStart' }, { kind: 'testStart', test: 'T1' }]);
  assertEqual(outcome(run), {
    text: 'Run ended without reporting a result -- the rig state is unknown; safe the rig', tone: 'fail',
  });
});

test('outcome: passed', () => {
  const { run } = play([{ kind: 'runStart' }, { kind: 'testEnd', test: 'T1', passed: true }, { kind: 'runEnd', allPassed: true }]);
  assertEqual(outcome(run), { text: 'PASSED -- 1 test', tone: 'pass' });
});

test('outcome: failed', () => {
  const { run } = play([
    { kind: 'runStart' },
    { kind: 'testEnd', test: 'T1', passed: true }, { kind: 'testEnd', test: 'T2', passed: false },
    { kind: 'runEnd', allPassed: false },
  ]);
  assertEqual(outcome(run), { text: 'FAILED -- 1 of 2', tone: 'fail' });
});
