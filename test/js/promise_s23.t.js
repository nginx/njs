/*---
includes: []
flags: [async]
---*/

var returnValue = null;
var resolve;

var promise = new Promise(function(_resolve) {
    resolve = _resolve;
});

promise
.then(
    v => $DONOTEVALUATE(),
    e => {

        assert.sameValue(e.constructor, TypeError,
                         'The promise should be rejected with a TypeError instance.');
})
.then($DONE, $DONE);

returnValue = resolve(promise);

assert.sameValue(returnValue, undefined);
