/*---
includes: []
flags: [async]
---*/

let called = false;

var thenable = Promise.resolve();
var p = new Promise(function(a,b) {
    throw thenable;
});

p
.then(v => $DONOTEVALUATE())
.then(
    v => $DONOTEVALUATE(),
    function(x) {
        assert.sameValue(x, thenable, 'The promise should be rejected with the resolution value.');
        called = true;
    }
)
.then(() => assert.sameValue(called, true))
.then($DONE, $DONE);
