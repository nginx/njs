/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

var promise = new Promise(function(resolve, reject) {
    stages.push('One');
    reject(42);
});

stages.push('Two');

promise.then(
    v => $DONOTEVALUATE(),
    v => assert.sameValue(v, 42))
.then(() => assert.compareArray(stages, ['One', 'Two', 'Three']))
.then($DONE, $DONE);

stages.push('Three');
