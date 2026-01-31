/*---
includes: []
flags: [async]
---*/

// Error.stack should be available in Promise.race rejection

var p1 = new Promise((resolve, reject) => {
    reject(new Error("race failed"));
});

Promise.race([p1])
.then(v => $DONOTEVALUATE())
.catch(e => {
    assert.sameValue(typeof e.stack, 'string');
})
.then($DONE, $DONE);
