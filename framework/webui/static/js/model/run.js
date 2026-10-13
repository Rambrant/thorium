// --- a run, as a record -----------------------------------------------
//
// What the page knows about the run it started, kept up to date from the
// stream by applyEvent. A plain object the view holds (state.run) and these
// functions change or read; nothing in it is on the page.

import { plural } from './format.js';

export function newRun(expected) {
  // `stopped` is what this page asked for, not what happened: null, 'stop' or
  // 'emergency'. `expected` is null for a run with no bound.
  return { sawRunStart: false, passed: undefined, finished: 0, failed: 0, errors: [], current: '', expected, stopped: null };
}

// Updates the record for one event, and says which test's verdict in the
// tree changed, if one did: { test, state }, state as model/catalog.js's
// verdictLabel names it.
export function applyEvent(run, e) {
  switch (e.kind) {
    case 'runStart':
      run.sawRunStart = true;
      return null;
    case 'testStart':
      return { test: e.test, state: 'running' };
    case 'testEnd':
      run.finished += 1;
      if (!e.passed) run.failed += 1;
      return { test: e.test, state: e.passed ? 'pass' : 'fail' };
    case 'runEnd':
      run.passed = e.allPassed;
      return null;
  }
  return null;
}

export function progressText(run) {
  if (run.expected === null) return plural(run.finished, 'test') + ' run -- ' + run.failed + ' failed';
  return run.finished + '/' + run.expected + ' -- ' + run.failed + ' failed';
}

// The four outcomes README.md's "What a run means" lists, told apart by the
// stream rather than by an exit code this page never sees.
export function outcome(run) {
  // Asked to end early and did: run_scripts reports that as not passed -- a run
  // that ended early did not make the claim a pass makes -- so what the page
  // says is its own. Tone follows the checks that did run, never the exit.
  if (run.stopped === 'stop' && run.passed === false) {
    return run.failed === 0
      ? { text: 'STOPPED -- ' + plural(run.finished, 'test') + ', none failed (the run did not finish)', tone: 'pass' }
      : { text: 'STOPPED -- ' + run.failed + ' of ' + run.finished + ' failed', tone: 'fail' };
  }

  if (!run.sawRunStart) {
    return { text: 'The run did not start' + (run.errors.length ? ': ' + run.errors.join(' ') : ''), tone: 'fail' };
  }
  if (run.passed === undefined) {
    return { text: 'Run ended without reporting a result -- the rig state is unknown; safe the rig', tone: 'fail' };
  }
  if (run.passed) return { text: 'PASSED -- ' + plural(run.finished, 'test'), tone: 'pass' };
  return { text: 'FAILED -- ' + run.failed + ' of ' + run.finished, tone: 'fail' };
}

// What the status line says while a run is going.
export function runningText(expected) {
  return expected === null ? 'Running until a test fails or you stop it...' : 'Running ' + plural(expected, 'test') + '...';
}

// What the page says once an emergency stop has been answered: { killed, safed }
// from /api/emergency. The run's logs are never written on this path, and the
// rig is only known idle if --safe reported so.
export function emergencyOutcome(reply) {
  if (!reply.safed) return { text: 'SAFING FAILED -- do not approach the fixture', tone: 'fail' };
  return {
    text: reply.killed
      ? 'EMERGENCY STOP -- run killed, rig safed. This run wrote no logs.'
      : 'Rig safed: all outputs off, all relays open.',
    tone: 'fail',
  };
}
