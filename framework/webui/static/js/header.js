// --- the header -------------------------------------------------------

import { $, el, plural } from './dom.js';

// What the manifest says about criteria; null when there is no manifest.
let manifest = null;

export function applyManifest(doc) {
  const variants = (doc && doc.criteriaVariants) || [];
  if (variants.length === 0) return;   // keep the free-text field

  manifest = doc;
  const select = el('select', { id: 'criteria' });
  for (const variant of variants) select.append(el('option', { value: variant, textContent: variant }));
  select.value = variants.includes(doc.defaultCriteriaVariant) ? doc.defaultCriteriaVariant : variants[0];
  $('criteria').replaceWith(select);
}

// The three header fields, as settings. A field left empty contributes no
// flag -- the operator field in particular, because run_scripts already
// falls back to $USER and a value this page filled in would be the page
// making a traceability claim on somebody's behalf.
export function headerSettings() {
  const settings = [];
  const push = (flag, value) => { if (value) settings.push({ flag, value, present: true }); };
  push('--dut-serial', $('dutSerial').value.trim());
  push('--operator', $('operator').value.trim());

  // --criteria only when moved off the build's own default, so an operator
  // who never touched it follows the default the day it changes.
  const criteria = $('criteria').value.trim();
  if (!manifest || criteria !== manifest.defaultCriteriaVariant) push('--criteria', criteria);
  return settings;
}

export function showRunInfo(info) {
  const box = $('runinfo');
  box.replaceChildren();
  const field = (key, value) => {
    if (!value) return;
    box.append(el('span', {}, el('span', { className: 'k', textContent: key }), value));
  };

  // Promoted above everything else, for the reason core/journal/journal.hpp
  // puts it in the header at all: it is the difference between watching a
  // DUT and watching a file.
  if (info.benchAttached === false) {
    box.append(el('span', { className: 'warn', textContent: 'NO BENCH ATTACHED -- readings are not from the rig' }));
  }
  field('DUT', [info.dutName, info.dutSerial].filter(Boolean).join(' / '));
  field('Rig', info.rigName);
  field('Criteria', info.criteriaVariant +
    (info.criteriaMaster && info.criteriaMaster !== info.criteriaVariant ? ' (unchanged rows from ' + info.criteriaMaster + ')' : ''));
  field('Operator', info.operator);
  field('Started', info.startedLocal);
  field('Suite', info.suiteVersion);
  if (Array.isArray(info.instruments) && info.instruments.length) {
    const inst = el('span', {}, el('span', { className: 'k', textContent: 'Instruments' }), plural(info.instruments.length, 'instrument'));
    inst.title = info.instruments.join('\n');
    box.append(inst);
  }
  box.hidden = false;
}
