/*---
includes: []
flags: []
paths: [test/js/module]
---*/

import m from 'sinking_export_default.js';

assert.sameValue(typeof m, 'object');
assert.sameValue(m.a, 42);
