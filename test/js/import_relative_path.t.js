/*---
includes: []
flags: []
paths: [test/js/module/]
---*/

import name from 'name.js';
import hash from 'libs/hash.js';

assert.sameValue(hash.name, "libs.name");
