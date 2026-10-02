// --- the header -------------------------------------------------------
//
// The three fields and the run's traceability strip. What they turn into
// is model/header.js's.

import { $, el } from './dom.js';
import * as model from './model/header.js';

// What run_scripts --describe-criteria said, via /api/criteria; null when it
// could not say, and the criteria field stays free text.
let criteria = null;

export function applyCriteria(doc) {
  const choice = model.criteriaChoice(doc);
  if (!choice) return;

  criteria = doc;
  const select = el('select', { id: 'criteria' });
  for (const variant of choice.variants) select.append(el('option', { value: variant, textContent: variant }));
  select.value = choice.selected;
  $('criteria').replaceWith(select);
}

export function headerSettings() {
  return model.headerSettings({
    dutSerial: $('dutSerial').value,
    operator: $('operator').value,
    criteria: $('criteria').value,
  }, criteria);
}

export function showRunInfo(info) {
  const { warning, fields } = model.runInfo(info);
  const box = $('runinfo');
  box.replaceChildren();
  if (warning) box.append(el('span', { className: 'warn', textContent: warning }));
  for (const { key, value, title } of fields) {
    box.append(el('span', { title }, el('span', { className: 'k', textContent: key }), value));
  }
  box.hidden = false;
}
