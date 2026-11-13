// --- Raw events -------------------------------------------------------
//
// Every line the stream carried, kept so the tab can be redrawn in either
// form model/format.js's rawText offers.
//
// Drawn in batches, and only the last kMaxLines of them. A soak run is
// hundreds of thousands of lines, and both halves of that used to end the
// page: each line was added with `textContent +=`, which rewrites everything
// already there, and nothing was ever let go.

import { $ } from './dom.js';
import { rawText } from './model/format.js';

const kMaxLines = 20000;
const kFlushMs = 50;

const log = $('log');
const notice = document.createTextNode('');
let rawLines = [];
let rawAsJson = false;
let dropped = 0;
// What has arrived since the last draw, and the text node each earlier draw
// left behind with how many lines it holds -- so the oldest can be removed
// whole, without touching the rest.
let pending = [];
let batches = [];
let timer = null;

function showDropped() {
  notice.textContent = dropped
    ? `... ${dropped} earlier lines are not shown; the saved log has every one ...\n`
    : '';
}

function flush() {
  timer = null;
  if (!pending.length) return;
  const follow = log.scrollTop + log.clientHeight >= log.scrollHeight - 4;
  const node = document.createTextNode(pending.map((line) => rawText(line, rawAsJson) + "\n").join(''));
  batches.push({ node, lines: pending.length });
  pending = [];
  log.append(node);

  while (rawLines.length > kMaxLines && batches.length > 1) {
    const oldest = batches.shift();
    oldest.node.remove();
    rawLines.splice(0, oldest.lines);
    dropped += oldest.lines;
  }
  showDropped();
  if (follow) log.scrollTop = log.scrollHeight;
}

export function append(line) {
  rawLines.push(line);
  pending.push(line);
  if (timer === null) timer = setTimeout(flush, kFlushMs);
}

function redrawRaw() {
  pending = [];
  const node = document.createTextNode(rawLines.map((line) => rawText(line, rawAsJson) + "\n").join(''));
  batches = [{ node, lines: rawLines.length }];
  log.replaceChildren(notice, node);
  log.scrollTop = log.scrollHeight;
}

export function clearRaw() {
  rawLines = [];
  pending = [];
  batches = [];
  dropped = 0;
  showDropped();
  log.replaceChildren(notice);
}

$('rawFormat').addEventListener('click', () => {
  rawAsJson = !rawAsJson;
  $('rawFormat').textContent = rawAsJson ? 'Show text' : 'Show JSON';
  redrawRaw();
});

clearRaw();
