/*---
includes: []
flags: [async]
---*/

var p0 = Promise.reject(1);
var p1 = Promise.reject(2);

Promise.any([p0, p1]).then(
    v => $DONOTEVALUATE(),
    v => assert.sameValue(v.constructor, AggregateError),
)
.then($DONE, $DONE);
