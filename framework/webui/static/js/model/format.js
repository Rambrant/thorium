// Text the page shows, as pure functions.
//
// Everything under model/ is the page's logic without the page: no document,
// no window, no fetch -- plain values in, plain values out. The modules one
// level up are the view, and do nothing a model function could do for them.
// That split is what lets tests/js/ check this logic without a server or a
// run, and it is the rule to keep: code that needs the page goes up there,
// and whatever it decides goes down here.

export const plural = (n, noun) => n + ' ' + noun + (n === 1 ? '' : 's');

// --- Raw events -------------------------------------------------------
//
// A text log, one aligned line per event, or each event as indented JSON.
// The text form leaves out only what repeats the line's own start -- the kind
// or verb, the time -- and bookkeeping no reader wants inline (sequence,
// wallClockMs, and numeric/unit, which value already says).

const kQuietKeys = new Set(['kind', 'verb', 'sequence', 'timeUtc', 'wallClockMs', 'numeric', 'unit']);

export function rawValue(value) {
  if (Array.isArray(value)) return '[' + value.map(rawValue).join(', ') + ']';
  const text = typeof value === 'string' ? value : JSON.stringify(value);
  return /[\s"]/.test(text) ? JSON.stringify(text) : text;
}

export function eventText(e) {
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

// One line the stream carried: { stderr } for a stderr line, otherwise
// { data, parsed } with parsed null when data was not JSON.
export function rawText(line, asJson) {
  if (line.stderr !== undefined) return '[stderr] ' + line.stderr;
  if (!line.parsed) return line.data;
  return asJson ? JSON.stringify(line.parsed, null, 2) : eventText(line.parsed);
}
