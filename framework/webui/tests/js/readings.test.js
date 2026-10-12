import { test, assertEqual } from './harness.js';
import { readingsSettings } from '../../static/js/model/readings.js';

test('readingsSettings: live contributes no flag, whatever the path field holds', () => {
  assertEqual(readingsSettings({ mode: 'live', path: 'old.tsv' }), { settings: [], error: null });
});

test('readingsSettings: record and replay are their flag with a trimmed path', () => {
  assertEqual(readingsSettings({ mode: 'record', path: ' r.tsv ' }).settings, [{ flag: '--record', value: 'r.tsv', present: true }]);
  assertEqual(readingsSettings({ mode: 'replay', path: 'r.tsv' }).settings, [{ flag: '--replay', value: 'r.tsv', present: true }]);
});

test('readingsSettings: a file mode with no path is an error, not a live run', () => {
  const { settings, error } = readingsSettings({ mode: 'replay', path: '  ' });
  assertEqual(settings, []);
  assertEqual(typeof error, 'string');
});
