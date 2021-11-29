/*---
includes: []
flags: []
---*/

let checkPoint = '';

Promise.reject.call(function(executor) {
    checkPoint += 'a';
    executor();

    checkPoint += 'b';
    executor(function() {}, function() {});

    checkPoint += 'c';
}, {});

assert.sameValue(checkPoint, 'abc');
