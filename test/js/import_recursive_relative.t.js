/*---
includes: []
flags: []
paths: [test/js/module/]
---*/

import m from './recursive_relative.js';

assert.sameValue(m, 42);
