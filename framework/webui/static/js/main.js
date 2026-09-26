// --- start-up ---------------------------------------------------------
//
// The page's entry point, loaded by index.html as a module. Each module it
// pulls in wires its own buttons as it loads -- modules run after the
// document is parsed, so every element they look up is already there -- and
// what is left for this one is asking the server for what the page shows
// first. run.js is imported only for the Run and Safe buttons it wires.

import { setStatus } from './dom.js';
import { buildTree, parseTestList, showCatalogError } from './catalog.js';
import { applyManifest } from './header.js';
import { probeSavableLogs } from './logs.js';
import './run.js';

probeSavableLogs();

// Held open for as long as this page is: it is how the launcher knows a
// console window is still open (see /api/presence in main.cpp). Nothing is
// read from it; EventSource's own reconnect covers a server restart.
new EventSource('/api/presence');

fetch('/api/manifest').then((r) => (r.ok ? r.json() : null)).then(applyManifest).catch(() => {});

fetch('/api/tests')
  .then((r) => { if (!r.ok) throw new Error(); return r.text(); })
  .then((text) => buildTree(parseTestList(text)))
  .catch(() => {
    showCatalogError('Could not ask run_scripts for its catalog.');
    setStatus('Could not run --list-tests', 'fail');
  });
