// --- record / replay --------------------------------------------------
//
// The source, the save box and the file. What they turn into is
// model/readings.js's.

import { $ } from './dom.js';
import * as model from './model/readings.js';

function current() {
  return { source: $('readingsSource').value, record: $('recordReadings').checked, path: $('readingsPath').value };
}

function refresh() {
  const { source, record } = current();
  // Saving is for the bench: run_scripts will not record a replay.
  $('recordReadings').disabled = source === model.REPLAY;
  const { label, shown } = model.pathField({ source, record });
  $('readingsPathLabel').textContent = label;
  $('pathField').hidden = !shown;
}

$('readingsSource').addEventListener('change', refresh);
$('recordReadings').addEventListener('change', refresh);
refresh();

export function readingsSettings() {
  return model.readingsSettings(current());
}

export function setReadingsDisabled(value) {
  for (const id of ['readingsSource', 'readingsPath']) $(id).disabled = value;
  $('recordReadings').disabled = value || $('readingsSource').value === model.REPLAY;
}
