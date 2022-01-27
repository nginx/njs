/*---
includes: []
flags: []
paths: [test/js/module/, test/js/module/libs/]
---*/

import name   from 'name.js';
import lib1   from 'lib1.js';
import lib2   from 'lib2.js';
import lib1_2 from 'lib1.js';

import crypto from 'crypto';
var h = crypto.createHash('md5');
var hash = h.update('AB').digest('hex');

assert.sameValue(name, "name");

assert.sameValue(lib1.name, "libs.name");

assert.sameValue(lib1.hash(), hash);
assert.sameValue(lib2.hash(), hash);

assert.sameValue(lib1.get(), 0);

assert.sameValue(lib1_2.get(), 0);

lib1.inc();

assert.sameValue(lib1.get(), 1);

assert.sameValue(lib1_2.get(), 1);
