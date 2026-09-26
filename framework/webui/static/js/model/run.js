// --- a run, as a record -----------------------------------------------
//
// What the page knows about the run it started, kept up to date from the
// stream by applyEvent. A plain object the view holds (state.run) and these
// functions change or read; nothing in it is on the page.

import { plural } from './format.js';

export function newRun(expected) {
  return { sawRunStart: false, passed: undefined, finished: 0, failed: 0, errors: [], current: '', expected };
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
  return run.finished + '/' + run.expected + ' -- ' + run.failed + ' failed';
}

// The four outcomes README.md's "What a run means" lists, told apart by the
// stream rather than by an exit code this page never sees.
export function outcome(run) {
  if (!run.sawRunStart) {
    return { text: 'The run did not start' + (run.errors.length ? ': ' + run.errors.join(' ') : ''), tone: 'fail' };
  }
  if (run.passed === undefined) {
    return { text: 'Run ended without reporting a result -- the rig state is unknown; safe the rig', tone: 'fail' };
  }
  if (run.passed) return { text: 'PASSED -- ' + plural(run.finished, 'test'), tone: 'pass' };
  return { text: 'FAILED -- ' + run.failed + ' of ' + run.finished, tone: 'fail' };
}
