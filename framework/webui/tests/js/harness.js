// The whole test framework for the console page's JavaScript: test(),
// three assertions, and run(), which writes a TAP-like report into the page
// for cmake/RunJsTests.cmake to read out of headless Chrome's --dump-dom.
//
// Home-made rather than a library, for the same reason the tests run in
// Chrome rather than Node: nothing to install. It only has to be good enough
// to say which check failed and what it saw instead.

const tests = [];

export function test(name, fn) {
  tests.push({ name, fn });
}

class AssertionError extends Error {}

function show(value) {
  return value === undefined ? 'undefined' : JSON.stringify(value);
}

function deepEqual(a, b) {
  if (Object.is(a, b)) return true;
  if (typeof a !== 'object' || typeof b !== 'object' || a === null || b === null) return false;
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  const keys = Object.keys(a);
  if (keys.length !== Object.keys(b).length) return false;
  return keys.every((key) => Object.prototype.hasOwnProperty.call(b, key) && deepEqual(a[key], b[key]));
}

// Deep, and blind to key order: what a model function returns is compared
// as data, not as the text JSON.stringify happens to make of it.
export function assertEqual(actual, expected, what) {
  if (!deepEqual(actual, expected)) {
    throw new AssertionError((what ? what + '\n' : '') + 'expected: ' + show(expected) + '\n' + 'actual:   ' + show(actual));
  }
}

export function assertTrue(value, what) {
  if (value !== true) throw new AssertionError((what || 'expected true') + ', got ' + show(value));
}

export function assertFalse(value, what) {
  if (value !== false) throw new AssertionError((what || 'expected false') + ', got ' + show(value));
}

// The last line is what RunJsTests.cmake decides on, so it is written only
// once every test has run: a page that stops half way -- a module that fails
// to load, a test that hangs -- has no verdict line, and that is a failure.
export async function run() {
  const lines = [];
  let failed = 0;
  for (const [index, { name, fn }] of tests.entries()) {
    try {
      await fn();
      lines.push('ok ' + (index + 1) + ' - ' + name);
    } catch (error) {
      failed += 1;
      lines.push('not ok ' + (index + 1) + ' - ' + name);
      const detail = error instanceof AssertionError ? error.message : String(error && error.stack || error);
      for (const line of detail.split('\n')) lines.push('    ' + line);
    }
  }
  lines.push('# ' + tests.length + ' tests, ' + failed + ' failed');
  lines.push('# result: ' + (failed === 0 && tests.length > 0 ? 'PASS' : 'FAIL'));
  document.getElementById('report').textContent = lines.join('\n');
}
