/*---
includes: []
flags: []
paths: [test/js/module]
---*/

import m from 'export_name.js';

assert.sameValue(m.prod(3,4), 12);
