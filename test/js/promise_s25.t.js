/*---
includes: []
flags: [async]
---*/

var resolve, reject;
var promise = new Promise(function(_resolve, _reject) {
    resolve = _resolve;
    reject = _reject;
});

var P = function(executor) {
    executor(resolve, reject);
    return promise;
};

Promise.resolve.call(P, promise)
.then(
    v => $DONOTEVALUATE(),
    e => {
        assert.sameValue(e.constructor, TypeError,
                         'The promise should be rejected with a TypeError instance.');
})
.then($DONE, $DONE);
