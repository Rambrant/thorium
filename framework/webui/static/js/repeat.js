// --- repeat -----------------------------------------------------------
//
// The pass count and the stop-at-first-failure box. What they turn into is
// model/repeat.js's.

import { $ } from './dom.js';
import * as model from './model/repeat.js';

export function repeatSettings() {
  return model.repeatSettings({ passes: $('passes').value, stopOnFailure: $('stopOnFailure').checked });
}
