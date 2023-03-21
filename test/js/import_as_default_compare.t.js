/*---
includes: []
flags: []
paths: [test/js/module]
---*/

import a from 'lib6.js';
import b from 'lib6-1.js';

assert.sameValue(a.a, 1);
assert.sameValue(a.b, 2);

assert.sameValue(a.a, b.a);
assert.sameValue(a.b, b.b);
