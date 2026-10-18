import { test, assertEqual, assertTrue } from './harness.js';
import { applyEvent, emergencyOutcome, newRun, outcome, progressText, runningText } from '../../static/js/model/run.js';

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
  assertEqual(outcome(run), { text: 'The run did not start', tone: 'fail', detail: '' });
  run.errors.push('Preflight failed: no free instrument for box DeskDmm: all 1 candidates were busy', 'see --list-tests');
  const { text, detail } = outcome(run);
  assertEqual(text, 'The run did not start: Preflight failed');
  assertEqual(detail, 'Preflight failed: no free instrument for box DeskDmm: all 1 candidates were busy see --list-tests');
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

test('progressText: an unbounded run counts what it has run, with no total', () => {
  const { run } = play([{ kind: 'runStart' }, { kind: 'testEnd', test: 'T1', passed: true }, { kind: 'testEnd', test: 'T2', passed: false }], null);
  assertEqual(progressText(run), '2 tests run -- 1 failed');
});

test('runningText', () => {
  assertEqual(runningText(3), 'Running 3 tests...');
  assertEqual(runningText(null), 'Running until a test fails or you stop it...');
});

// run_scripts reports a stopped run as not passed; the page says what happened.
test('outcome: stopped with nothing failed is STOPPED, not FAILED, and reads as a pass', () => {
  const { run } = play([{ kind: 'runStart' }, { kind: 'testEnd', test: 'T1', passed: true }, { kind: 'runEnd', allPassed: false }], null);
  run.stopped = 'stop';
  assertEqual(outcome(run), { text: 'STOPPED -- 1 test, none failed (the run did not finish)', tone: 'pass' });
});

test('outcome: stopped after a failure says how many failed', () => {
  const { run } = play([
    { kind: 'runStart' }, { kind: 'testEnd', test: 'T1', passed: false }, { kind: 'testEnd', test: 'T2', passed: true },
    { kind: 'runEnd', allPassed: false },
  ], null);
  run.stopped = 'stop';
  assertEqual(outcome(run), { text: 'STOPPED -- 1 of 2 failed', tone: 'fail' });
});

test('outcome: a stop that arrived too late to matter is still PASSED', () => {
  const { run } = play([{ kind: 'runStart' }, { kind: 'testEnd', test: 'T1', passed: true }, { kind: 'runEnd', allPassed: true }]);
  run.stopped = 'stop';
  assertEqual(outcome(run).text, 'PASSED -- 1 test');
});

test('outcome: a stopped run with no runEnd is still the unknown-rig outcome', () => {
  const { run } = play([{ kind: 'runStart' }, { kind: 'testStart', test: 'T1' }]);
  run.stopped = 'stop';
  assertEqual(outcome(run).tone, 'fail');
});

test('emergencyOutcome: killed and safed, killed but not safed, and nothing to kill', () => {
  assertEqual(emergencyOutcome({ killed: true, safed: true }).text, 'EMERGENCY STOP -- run killed, rig safed. This run wrote no logs.');
  assertEqual(emergencyOutcome({ killed: true, safed: false }).text, 'SAFING FAILED -- do not approach the fixture');
  assertEqual(emergencyOutcome({ killed: false, safed: true }).text, 'Rig safed: all outputs off, all relays open.');
});
