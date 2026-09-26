// --- the header, as values --------------------------------------------

import { plural } from './format.js';

// What the criteria field should offer: the manifest's variants and the one
// to start on, or null to keep the free-text field -- no manifest, or one
// listing no variants.
export function criteriaChoice(manifest) {
  const variants = (manifest && manifest.criteriaVariants) || [];
  if (variants.length === 0) return null;
  const selected = variants.includes(manifest.defaultCriteriaVariant) ? manifest.defaultCriteriaVariant : variants[0];
  return { variants, selected };
}

// The three header fields, as settings. A field left empty contributes no
// flag -- the operator field in particular, because run_scripts already
// falls back to $USER and a value this page filled in would be the page
// making a traceability claim on somebody's behalf.
//
// `manifest` is the one criteriaChoice accepted, or null.
export function headerSettings(fields, manifest) {
  const settings = [];
  const push = (flag, value) => { if (value) settings.push({ flag, value, present: true }); };
  push('--dut-serial', fields.dutSerial.trim());
  push('--operator', fields.operator.trim());

  // --criteria only when moved off the build's own default, so an operator
  // who never touched it follows the default the day it changes.
  const criteria = fields.criteria.trim();
  if (!manifest || criteria !== manifest.defaultCriteriaVariant) push('--criteria', criteria);
  return settings;
}

// The run's traceability header -- runStart's info -- as a warning, if any,
// and the fields to show in order: { key, value, title }. A field the run did
// not report is left out rather than shown empty.
export function runInfo(info) {
  // Promoted above everything else, for the reason core/journal/journal.hpp
  // puts it in the header at all: it is the difference between watching a
  // DUT and watching a file.
  const warning = info.benchAttached === false ? 'NO BENCH ATTACHED -- readings are not from the rig' : null;

  const fields = [];
  const field = (key, value, title) => { if (value) fields.push({ key, value, title: title || '' }); };
  field('DUT', [info.dutName, info.dutSerial].filter(Boolean).join(' / '));
  field('Rig', info.rigName);
  field('Criteria', info.criteriaVariant &&
    info.criteriaVariant +
    (info.criteriaMaster && info.criteriaMaster !== info.criteriaVariant ? ' (unchanged rows from ' + info.criteriaMaster + ')' : ''));
  field('Operator', info.operator);
  field('Started', info.startedLocal);
  field('Suite', info.suiteVersion);
  if (Array.isArray(info.instruments) && info.instruments.length) {
    field('Instruments', plural(info.instruments.length, 'instrument'), info.instruments.join('\n'));
  }
  return { warning, fields };
}
