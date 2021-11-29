/*---
includes: []
flags: [async]
---*/

var p0 = Promise.resolve().then(() => { throw 'foo' });
var p1 = Promise.reject(2).catch(v => v * 2);
var p2 = Promise.resolve(1).then(v => v + 1);

Promise.any([p0, p1, p2]).then(
    v => assert.sameValue(v, 4),
    v => $DONOTEVALUATE(),
)
.then($DONE, $DONE);
