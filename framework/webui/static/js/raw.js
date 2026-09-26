// --- Raw events -------------------------------------------------------
//
// Every line the stream carried, kept so the tab can be redrawn in either
// form model/format.js's rawText offers.

import { $ } from './dom.js';
import { rawText } from './model/format.js';

const log = $('log');
let rawLines = [];
let rawAsJson = false;

export function append(line) {
  rawLines.push(line);
  const follow = log.scrollTop + log.clientHeight >= log.scrollHeight - 4;
  log.textContent += rawText(line, rawAsJson) + "\n";
  if (follow) log.scrollTop = log.scrollHeight;
}

function redrawRaw() {
  log.textContent = rawLines.map((line) => rawText(line, rawAsJson)).join("\n") + (rawLines.length ? "\n" : '');
  log.scrollTop = log.scrollHeight;
}

export function clearRaw() {
  rawLines = [];
  log.textContent = '';
}

$('rawFormat').addEventListener('click', () => {
  rawAsJson = !rawAsJson;
  $('rawFormat').textContent = rawAsJson ? 'Show text' : 'Show JSON';
  redrawRaw();
});
