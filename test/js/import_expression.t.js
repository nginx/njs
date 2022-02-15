/*---
includes: []
flags: []
paths: [test/js/module]
---*/

import m from 'export_expression.js';

assert.sameValue(m.sum(3,4), 7);
