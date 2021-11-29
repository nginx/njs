/*---
includes: []
flags: [async]
---*/

var p1 = new Promise((resolve, reject) => {
    throw 'one';
});

var p2 = new Promise((resolve, reject) => {
    setTimeout(resolve, 0, 'two');
});

Promise.race([p1, p2]).then(
    v => $DONOTEVALUATE(),
    v => assert.sameValue(v, 'one'),
)
.then($DONE, $DONE);
