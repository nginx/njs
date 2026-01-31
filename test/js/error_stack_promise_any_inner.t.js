/*---
includes: []
flags: [async]
---*/

// Inner errors in AggregateError.errors should have stack traces

var p1 = Promise.reject(new Error("inner error 1"));
var p2 = Promise.reject(new Error("inner error 2"));

Promise.any([p1, p2])
.then(v => $DONOTEVALUATE())
.catch(e => {
    assert.sameValue(e.name, 'AggregateError');
    assert.sameValue(e.errors.length, 2);
    assert.sameValue(typeof e.errors[0].stack, 'string');
    assert.sameValue(typeof e.errors[1].stack, 'string');
})
.then($DONE, $DONE);
