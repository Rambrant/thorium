// --- record / replay --------------------------------------------------
//
// The mode and the file. What they turn into is model/readings.js's.

import { $ } from './dom.js';
import * as model from './model/readings.js';

function showPath() {
  $('readingsPath').hidden = $('readingsMode').value === model.LIVE;
}

$('readingsMode').addEventListener('change', showPath);

export function readingsSettings() {
  return model.readingsSettings({ mode: $('readingsMode').value, path: $('readingsPath').value });
}

export function setReadingsDisabled(value) {
  $('readingsMode').disabled = value;
  $('readingsPath').disabled = value;
}
