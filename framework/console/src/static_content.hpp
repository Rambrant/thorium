#pragma once

namespace console
{
    //
    // The entire browser side of this program, for now: one file, embedded
    // rather than read from disk. A real deployment's needs (a catalog tree,
    // the generated options dialog framework/ui/README.md Sec.3 describes)
    // are future work -- this exists to prove the pipe end to end, the same
    // way run_scripts's own console view did before this program existed.
    //
    // Embedded so the binary has no path to get wrong: wherever
    // thorium_console.exe ends up -- built in place, copied to a bench,
    // launched by framework/launcher from a shortcut -- this comes with it.
    // set_mount_point() and a real static/ directory are the obvious next
    // step once there is more than one file to serve.
    //
    inline constexpr const char *  kIndexHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Thorium Bench Console</title>
<style>
  body { font-family: system-ui, sans-serif; margin: 1.5rem; background: #1e1e1e; color: #ddd; }
  button { font-size: 1rem; padding: 0.5rem 1rem; margin-right: 0.5rem; cursor: pointer; }
  #safe { background: #7a1f1f; color: white; border: none; border-radius: 4px; }
  #run { background: #1f5c3a; color: white; border: none; border-radius: 4px; }
  #log { background: #111; color: #9f9; font-family: monospace; padding: 1rem;
         height: 60vh; overflow-y: auto; white-space: pre-wrap; margin-top: 1rem; border-radius: 4px; }
  #status { margin-top: 0.5rem; color: #aaa; }
</style>
</head>
<body>
  <h1>Thorium Bench Console</h1>
  <button id="run">Run all tests</button>
  <button id="safe">Safe the rig</button>
  <div id="status"></div>
  <div id="log"></div>

<script>
const log = document.getElementById('log');
const status = document.getElementById('status');

function append(text) {
  log.textContent += text + "\n";
  log.scrollTop = log.scrollHeight;
}

let currentSource = null;

function watchEvents() {
  if (currentSource) currentSource.close();
  currentSource = new EventSource('/api/events');
  currentSource.onmessage = (event) => {
    try {
      const parsed = JSON.parse(event.data);
      if (parsed.kind === 'stderr') {
        append('[stderr] ' + parsed.text);
        return;
      }
    } catch (e) { /* not JSON we recognise -- fall through and show it raw */ }
    append(event.data);
  };
  currentSource.onerror = () => { currentSource.close(); status.textContent = 'Run finished (or the console lost the connection).'; };
}

document.getElementById('run').addEventListener('click', async () => {
  log.textContent = '';
  status.textContent = 'Starting run...';
  const response = await fetch('/api/run', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ selection: [], settings: [], extra: [] }),
  });
  if (response.status === 409) {
    status.textContent = 'A run is already active.';
    return;
  }
  status.textContent = 'Run started.';
  watchEvents();
});

document.getElementById('safe').addEventListener('click', async () => {
  status.textContent = 'Safing the rig...';
  const response = await fetch('/safe', { method: 'POST' });
  const body = await response.json();
  status.textContent = body.ok ? 'Rig safed.' : 'Safe failed to start.';
});

fetch('/api/tests').then(r => r.text()).then(text => {
  console.log('tests:', text);
});
fetch('/api/options').then(r => r.text()).then(text => {
  console.log('options:', text);
});
</script>
</body>
</html>
)HTML";
}
