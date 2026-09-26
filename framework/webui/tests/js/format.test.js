import { test, assertEqual } from './harness.js';
import { eventText, plural, rawText, rawValue } from '../../static/js/model/format.js';

test('plural: one is singular, zero and many are not', () => {
  assertEqual(plural(1, 'test'), '1 test');
  assertEqual(plural(0, 'test'), '0 tests');
  assertEqual(plural(12, 'check'), '12 checks');
});

test('rawValue: bare when it can be, quoted when it has spaces or quotes', () => {
  assertEqual(rawValue('Vout'), 'Vout');
  assertEqual(rawValue(3.3), '3.3');
  assertEqual(rawValue(true), 'true');
  assertEqual(rawValue('3.30 V'), '"3.30 V"');
  assertEqual(rawValue('say "hi"'), '"say \\"hi\\""');
  assertEqual(rawValue(['a', 'b c']), '[a, "b c"]');
});

test('eventText: time and verb lead, bookkeeping is left out', () => {
  const line = eventText({
    kind: 'event', verb: 'Measure', sequence: 7, timeUtc: '2026-09-27T10:11:12.345Z', wallClockMs: 99,
    subject: 'Vout', value: '3.30 V', numeric: 3.3, unit: 'V',
  });
  assertEqual(line, '10:11:12.345  Measure     subject=Vout  value="3.30 V"');
});

test('eventText: a non-event is named by its kind, and a missing time is padded', () => {
  assertEqual(eventText({ kind: 'testEnd', test: 'T1', passed: true }), ''.padEnd(12) + '  testEnd     test=T1  passed=true');
});

test("eventText: runStart's info is flattened one level", () => {
  const line = eventText({ kind: 'runStart', info: { dutSerial: 'SN-1', rigName: 'bench 2' } });
  assertEqual(line, ''.padEnd(12) + '  runStart    dutSerial=SN-1  rigName="bench 2"');
});

test('rawText: stderr, unparsed, text and JSON forms', () => {
  assertEqual(rawText({ stderr: 'no such test' }, false), '[stderr] no such test');
  assertEqual(rawText({ data: 'not json', parsed: null }, false), 'not json');
  const parsed = { kind: 'runEnd', allPassed: true };
  assertEqual(rawText({ data: '', parsed }, true), JSON.stringify(parsed, null, 2));
  assertEqual(rawText({ data: '', parsed }, false), eventText(parsed));
});
