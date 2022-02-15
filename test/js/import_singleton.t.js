/*---
includes: []
flags: []
paths: [test/js/module/, test/js/module/libs/]
---*/

import lib1   from 'lib1.js';
import lib1_2 from 'lib1.js';

assert.sameValue(lib1.get(), 0);
assert.sameValue(lib1_2.get(), 0);

lib1.inc();

assert.sameValue(lib1.get(), 1);
assert.sameValue(lib1_2.get(), 1);
