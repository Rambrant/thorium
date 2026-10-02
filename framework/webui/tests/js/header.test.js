import { test, assertEqual } from './harness.js';
import { criteriaChoice, headerSettings, runInfo } from '../../static/js/model/header.js';

const described = { criteriaVariants: ['production', 'stress', 'aged'], defaultCriteriaVariant: 'stress' };

test('criteriaChoice: the variants, starting on the default', () => {
  assertEqual(criteriaChoice(described), { variants: ['production', 'stress', 'aged'], selected: 'stress' });
});

test('criteriaChoice: the first variant when the default is not among them', () => {
  assertEqual(criteriaChoice({ criteriaVariants: ['a', 'b'], defaultCriteriaVariant: 'gone' }).selected, 'a');
});

test('criteriaChoice: no answer, or no variants, keeps the free-text field', () => {
  assertEqual(criteriaChoice(null), null);
  assertEqual(criteriaChoice({ criteriaVariants: [] }), null);
  assertEqual(criteriaChoice({}), null);
});

const flags = (settings) => settings.map((s) => s.flag + '=' + s.value);

test('headerSettings: empty fields contribute no flag -- operator included', () => {
  assertEqual(headerSettings({ dutSerial: '  ', operator: '', criteria: 'stress' }, described), []);
});

test('headerSettings: trimmed values, each marked present', () => {
  const settings = headerSettings({ dutSerial: ' SN-7 ', operator: 'Ann', criteria: 'stress' }, described);
  assertEqual(settings, [
    { flag: '--dut-serial', value: 'SN-7', present: true },
    { flag: '--operator', value: 'Ann', present: true },
  ]);
});

test('headerSettings: --criteria only when moved off the default', () => {
  assertEqual(flags(headerSettings({ dutSerial: '', operator: '', criteria: 'aged' }, described)), ['--criteria=aged']);
  assertEqual(flags(headerSettings({ dutSerial: '', operator: '', criteria: 'stress' }, described)), []);
});

test('headerSettings: without an answer, whatever was typed is passed', () => {
  assertEqual(flags(headerSettings({ dutSerial: '', operator: '', criteria: 'stress' }, null)), ['--criteria=stress']);
  assertEqual(flags(headerSettings({ dutSerial: '', operator: '', criteria: '' }, null)), []);
});

test('runInfo: the fields in order, the unreported left out', () => {
  const { warning, fields } = runInfo({
    dutName: 'PSU', dutSerial: 'SN-7', criteriaVariant: 'stress', criteriaMaster: 'stress', operator: 'Ann',
  });
  assertEqual(warning, null);
  assertEqual(fields, [
    { key: 'DUT', value: 'PSU / SN-7', title: '' },
    { key: 'Criteria', value: 'stress', title: '' },
    { key: 'Operator', value: 'Ann', title: '' },
  ]);
});

test('runInfo: a criteria master other than the variant is named', () => {
  const { fields } = runInfo({ criteriaVariant: 'stress', criteriaMaster: 'production' });
  assertEqual(fields, [{ key: 'Criteria', value: 'stress (unchanged rows from production)', title: '' }]);
});

test('runInfo: no criteria reported is no Criteria field, not "undefined"', () => {
  assertEqual(runInfo({ criteriaMaster: 'production' }).fields, []);
});

test('runInfo: only benchAttached === false warns', () => {
  assertEqual(runInfo({ benchAttached: false }).warning, 'NO BENCH ATTACHED -- readings are not from the rig');
  assertEqual(runInfo({ benchAttached: true }).warning, null);
  assertEqual(runInfo({}).warning, null);
});

test('runInfo: instruments are counted, and listed in the tooltip', () => {
  const { fields } = runInfo({ instruments: ['dmm-1', 'psu-1'] });
  assertEqual(fields, [{ key: 'Instruments', value: '2 instruments', title: 'dmm-1\npsu-1' }]);
  assertEqual(runInfo({ instruments: [] }).fields, []);
});
