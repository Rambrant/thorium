// What more than one module both reads and changes. On one object rather than
// as exported lets, because an imported binding is read-only to its importer:
// catalog.js could see run.js's `running` change, but never set it itself.
export const state = {
  // A run is active: the header, the tree's checkboxes and Run are locked.
  running: false,

  // Stop has been pressed for the run in progress, and the button waits for
  // the run to say it has ended.
  stopping: false,

  // The run this page started, from Run's click until the next one -- see
  // run.js. null until then, which probeSavableLogs relies on.
  run: null,
};
