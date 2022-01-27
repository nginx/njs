/*---
includes: []
flags: []
paths: [test/js/module]
---*/

import m from 'export_comma_expression.js';

assert.sameValue(m.prod(3,5), 15);
