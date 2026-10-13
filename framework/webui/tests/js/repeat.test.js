import { test, assertEqual } from './harness.js';
import { repeatSettings, expectedTests } from '../../static/js/model/repeat.js';

const flags = (r) => r.settings.map((s) => s.flag + (s.value ? '=' + s.value : ''));

test('repeatSettings: untouched controls contribute no flag, and one pass', () => {
  const r = repeatSettings({ passes: '  ', stopOnFailure: false });
  assertEqual([r.settings, r.passes, r.error], [[], 1, null]);
});

test('repeatSettings: a count is --repeat, and is the pass count', () => {
  const r = repeatSettings({ passes: ' 50 ', stopOnFailure: false });
  assertEqual([flags(r), r.passes, r.error], [['--repeat=50'], 50, null]);
});

test('repeatSettings: stopping at the first failure adds --until-failure after the count', () => {
  assertEqual(flags(repeatSettings({ passes: '10', stopOnFailure: true })), ['--repeat=10', '--until-failure']);
});

test('repeatSettings: stopping at the first failure without a count is unbounded, and says so', () => {
  const r = repeatSettings({ passes: '', stopOnFailure: true });
  assertEqual([flags(r), r.passes, r.error], [['--until-failure'], null, null]);
});

test('repeatSettings: zero, negative, fractional and non-numeric counts are refused', () => {
  for (const passes of ['0', '-3', '2.5', 'ten', '1e3']) {
    assertEqual(typeof repeatSettings({ passes, stopOnFailure: false }).error, 'string');
  }
});

test('expectedTests: the selection, once per pass -- and nothing to expect when unbounded', () => {
  assertEqual(expectedTests(9, 3), 27);
  assertEqual(expectedTests(9, null), null);
});
