/*---
includes: []
flags: []
paths: [test/js/module/, test/js/module/libs/, test/js/module/sub]
---*/

import lib2 from 'lib2.js';

assert.sameValue(lib2.hash(), "XXX");
