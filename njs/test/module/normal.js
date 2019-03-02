import lib1   from 'lib1.js';
import lib2   from 'lib2.js';
import lib1_2 from 'lib1.js';

import crypto from 'crypto';
var h = crypto.createHash('md5');
var hash = h.update('AB').digest('hex');

if (lib1.hash() != hash) {
    console.log("failed!");
}

if (lib2.hash() != hash) {
    console.log("failed!");
}

if (lib1.get() != 0) {
    console.log("failed!");
}

if (lib1_2.get() != 0) {
    console.log("failed!");
}

lib1.inc();

if (lib1.get() != 1) {
    console.log("failed!");
}

if (lib1_2.get() != 1) {
    console.log("failed!");
}

console.log("passed!");
