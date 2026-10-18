import { test, assertEqual } from './harness.js';
import { repeatSettings, countField, expectedTests } from '../../static/js/model/repeat.js';

const flags = (r) => r.settings.map((s) => s.flag + (s.value ? '=' + s.value : ''));

test('repeatSettings: once contributes no flag, and is one pass -- whatever the count field holds', () => {
  const r = repeatSettings({ mode: 'once', count: '50' });
  assertEqual([r.settings, r.passes, r.error], [[], 1, null]);
});

test('repeatSettings: a number of times is --repeat, and is the pass count', () => {
  const r = repeatSettings({ mode: 'times', count: ' 50 ' });
  assertEqual([flags(r), r.passes, r.error], [['--repeat=50'], 50, null]);
});

test('repeatSettings: a number of times needs the number', () => {
  const r = repeatSettings({ mode: 'times', count: '' });
  assertEqual(r.settings, []);
  assertEqual(typeof r.error, 'string');
});

test('repeatSettings: until a test fails, with no limit, is unbounded and says so', () => {
  const r = repeatSettings({ mode: 'until', count: '  ' });
  assertEqual([flags(r), r.passes, r.error], [['--until-failure'], null, null]);
});

test('repeatSettings: until a test fails, at most N, is both flags and a bounded count', () => {
  const r = repeatSettings({ mode: 'until', count: '10' });
  assertEqual([flags(r), r.passes, r.error], [['--repeat=10', '--until-failure'], 10, null]);
});

test('repeatSettings: zero, negative, fractional and non-numeric counts are refused', () => {
  for (const mode of ['times', 'until']) {
    for (const count of ['0', '-3', '2.5', 'ten', '1e3']) {
      assertEqual(typeof repeatSettings({ mode, count }).error, 'string');
    }
  }
});

test('countField: shown for the two repeating modes, labelled for what it means', () => {
  assertEqual(countField('once').shown, false);
  assertEqual(countField('times').label, 'Times');
  assertEqual(countField('until').label, 'At most (optional)');
  assertEqual(countField('until').shown, true);
});

test('expectedTests: the selection, once per pass -- and nothing to expect when unbounded', () => {
  assertEqual(expectedTests(9, 3), 27);
  assertEqual(expectedTests(9, null), null);
});
