// --- how often to run -------------------------------------------------
//
// The "Run" choice and its count. What they turn into is model/repeat.js's.

import { $ } from './dom.js';
import * as model from './model/repeat.js';

function refresh() {
  const { label, placeholder, shown } = model.countField($('repeatMode').value);
  $('countLabel').textContent = label;
  $('repeatCount').placeholder = placeholder;
  $('countField').hidden = !shown;
}

$('repeatMode').addEventListener('change', refresh);
refresh();

export function repeatSettings() {
  return model.repeatSettings({ mode: $('repeatMode').value, count: $('repeatCount').value });
}
