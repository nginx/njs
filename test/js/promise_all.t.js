/*---
includes: [compareArray.js]
flags: [async]
---*/

function resolve(value) {
    return new Promise(resolve => setTimeout(() => resolve(value), 0));
}

Promise.all([resolve(['one', 'two']), resolve(['three', 'four'])])
.then(
    v => { assert.compareArray(v[0], ['one', 'two']);
           assert.compareArray(v[1], ['three', 'four']); },
    v => $DONOTEVALUATE(),
)
.then($DONE, $DONE);
