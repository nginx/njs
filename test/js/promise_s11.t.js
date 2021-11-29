/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

var promise = new Promise((resolve, reject) => resolve('all'));

promise.then( function f1(result) {
    stages.push('S: ' + result);
    return 'f1';
});

promise.then( function f2(result) {
    stages.push('R: ' + result);
    return 'f2';
})
.then(() => assert.compareArray(stages, ['end', 'S: all', 'R: all']))
.then($DONE, $DONE);

stages.push('end');
