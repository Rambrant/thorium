// Every test module, then the run. A new *.test.js is one more import here.

import './format.test.js';
import './catalog.test.js';
import './results.test.js';
import './header.test.js';
import './run.test.js';
import './logs.test.js';
import { run } from './harness.js';

await run();
