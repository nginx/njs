/*---
includes: []
flags: [async]
---*/

var returnValue = null;
var value = {};
var resolve;

var thenable = new Promise(function(resolve) {
    resolve();
});

var promise = new Promise(function(_resolve) {
    resolve = _resolve;
});

thenable.then = function(resolve) {
    resolve(value);
};

promise
.then(
    v => {
        assert.sameValue(v, value, 'The promise should be fulfilled with the provided value.');
    },
    e => $DONOTEVALUATE)
.then($DONE, $DONE);

returnValue = resolve(thenable);

assert.sameValue(returnValue, undefined);
