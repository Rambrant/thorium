import { test, assertEqual } from './harness.js';
import { readingsSettings, pathField } from '../../static/js/model/readings.js';

test('readingsSettings: the bench with nothing saved contributes no flag, whatever the path field holds', () => {
  assertEqual(readingsSettings({ source: 'live', record: false, path: 'old.tsv' }), { settings: [], error: null });
});

test('readingsSettings: saving from the bench is --record, with a trimmed path', () => {
  assertEqual(readingsSettings({ source: 'live', record: true, path: ' r.tsv ' }).settings, [{ flag: '--record', value: 'r.tsv', present: true }]);
});

test('readingsSettings: a replay is --replay', () => {
  assertEqual(readingsSettings({ source: 'replay', record: false, path: 'r.tsv' }).settings, [{ flag: '--replay', value: 'r.tsv', present: true }]);
});

test('readingsSettings: a replay never records, even with the box left ticked', () => {
  assertEqual(readingsSettings({ source: 'replay', record: true, path: 'r.tsv' }).settings.map((s) => s.flag), ['--replay']);
});

test('readingsSettings: a file mode with no path is an error, not a live run', () => {
  for (const [source, record] of [['replay', false], ['live', true]]) {
    const { settings, error } = readingsSettings({ source, record, path: '  ' });
    assertEqual(settings, []);
    assertEqual(typeof error, 'string');
  }
});

test('pathField: named for what it is, and shown only when there is a file', () => {
  assertEqual(pathField({ source: 'live', record: false }).shown, false);
  assertEqual(pathField({ source: 'live', record: true }), { label: 'Record to', shown: true });
  assertEqual(pathField({ source: 'replay', record: true }), { label: 'Replay from', shown: true });
});
