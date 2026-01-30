/*---
includes: []
flags: [async]
---*/

// Error.stack should be available in Promise.all rejection

var p1 = new Promise((resolve, reject) => {
    reject(new Error("promise failed"));
});
var p2 = Promise.resolve("ok");

Promise.all([p1, p2])
.then(v => $DONOTEVALUATE())
.catch(e => {
    assert.sameValue(typeof e.stack, 'string');
})
.then($DONE, $DONE);
