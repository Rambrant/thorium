import { test, assertEqual } from './harness.js';
import { logFileName, saveLogState } from '../../static/js/model/logs.js';

test('logFileName: from Content-Disposition, or the kind', () => {
  assertEqual(logFileName('attachment; filename="run-2026-09-27.rtf"', 'rtf'), 'run-2026-09-27.rtf');
  assertEqual(logFileName('attachment', 'sarif'), 'sarif');
  assertEqual(logFileName(null, 'rtf'), 'rtf');
});

const savable = { rtf: 'run.rtf' };

test('saveLogState: a log the run wrote, once it is over', () => {
  assertEqual(saveLogState(savable, 'rtf', false), { disabled: false, title: 'run.rtf' });
});

test('saveLogState: nothing while running, even a log that exists', () => {
  assertEqual(saveLogState(savable, 'rtf', true), { disabled: true, title: 'Available when the run has finished' });
});

test('saveLogState: a log the run did not write', () => {
  assertEqual(saveLogState(savable, 'sarif', false), { disabled: true, title: 'The last run wrote no such log' });
  assertEqual(saveLogState({}, 'rtf', false).disabled, true);
});
