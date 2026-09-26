// --- Raw events -------------------------------------------------------
//
// Every line the stream carried, kept so the tab can be redrawn in either
// form: a text log, one aligned line per event, or each event as indented
// JSON. The text form leaves out only what repeats the line's own start --
// the kind or verb, the time -- and bookkeeping no reader wants inline
// (sequence, wallClockMs, and numeric/unit, which value already says).

import { $ } from './dom.js';

const log = $('log');
let rawLines = [];
let rawAsJson = false;

const kQuietKeys = new Set(['kind', 'verb', 'sequence', 'timeUtc', 'wallClockMs', 'numeric', 'unit']);

function rawValue(value) {
  if (Array.isArray(value)) return '[' + value.map(rawValue).join(', ') + ']';
  const text = typeof value === 'string' ? value : JSON.stringify(value);
  return /[\s"]/.test(text) ? JSON.stringify(text) : text;
}

function eventText(e) {
  const time = e.timeUtc ? e.timeUtc.slice(11, 23) : ''.padEnd(12);
  const what = (e.kind === 'event' ? e.verb : e.kind) || '?';
  const fields = [];
  for (const [key, value] of Object.entries(e)) {
    if (kQuietKeys.has(key)) continue;
    if (value && typeof value === 'object' && !Array.isArray(value)) {
      // runStart's info: one level, flattened, rather than a JSON blob.
      for (const [inner, v] of Object.entries(value)) fields.push(inner + '=' + rawValue(v));
    } else {
      fields.push(key + '=' + rawValue(value));
    }
  }
  return time + '  ' + what.padEnd(11) + ' ' + fields.join('  ');
}

function rawText(line) {
  if (line.stderr !== undefined) return '[stderr] ' + line.stderr;
  if (!line.parsed) return line.data;
  return rawAsJson ? JSON.stringify(line.parsed, null, 2) : eventText(line.parsed);
}

export function append(line) {
  rawLines.push(line);
  const follow = log.scrollTop + log.clientHeight >= log.scrollHeight - 4;
  log.textContent += rawText(line) + "\n";
  if (follow) log.scrollTop = log.scrollHeight;
}

function redrawRaw() {
  log.textContent = rawLines.map(rawText).join("\n") + (rawLines.length ? "\n" : '');
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
