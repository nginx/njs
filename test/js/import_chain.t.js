/*---
includes: []
flags: []
paths: [test/js/module/, test/js/module/libs/]
---*/

import lib2   from 'lib2.js';

import crypto from 'crypto';
var h = crypto.createHash('md5');
var hash = h.update('AB').digest('hex');

assert.sameValue(lib2.hash(), hash);
