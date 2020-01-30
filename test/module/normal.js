import name   from 'name.js';
import lib1   from 'lib1.js';
import lib2   from 'lib2.js';
import lib1_2 from 'lib1.js';

import crypto from 'crypto';
var h = crypto.createHash('md5');
var hash = h.update('AB').digest('hex');

var fails = 0;

if (name != 'name') {
    fails++;
}

if (lib1.name != 'libs.name') {
    fails++;
}

if (lib1.hash() != hash) {
    fails++;
}

if (lib2.hash() != hash) {
    fails++;
}

if (lib1.get() != 0) {
    fails++;
}

if (lib1_2.get() != 0) {
    fails++;
}

lib1.inc();

if (lib1.get() != 1) {
    fails++;
}

if (lib1_2.get() != 1) {
    fails++;
}

if (JSON.stringify({}) != "{}") {
    fails++;
}

setImmediate(console.log,
             fails ? "failed: " + fails : "passed!");
