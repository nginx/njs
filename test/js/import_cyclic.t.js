/*---
includes: []
flags: []
paths: [test/js/module/]
---*/

import a from 'cyclic_a.js';
import b from 'cyclic_b.js';

assert.sameValue(a, 10);
assert.sameValue(b, 11);
