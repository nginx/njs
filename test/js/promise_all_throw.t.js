/*---
includes: []
flags: [async]
---*/

var p0 = Promise.resolve(1).then(v => v + 1);
var p1 = Promise.reject(2).catch(v => v * 2);
var p2 = Promise.resolve().then(() => { throw 'foo' });
var p3 = Promise.reject().then(() => { throw 'oof'; });

Promise.all([p0, p1, p2, p3]).then(
    v => $DONOTEVALUATE(),
    v => assert.sameValue(v, 'foo')
)
.then($DONE, $DONE);
