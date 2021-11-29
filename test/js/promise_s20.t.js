/*---
includes: []
flags: [async]
---*/

var returnValue = null;
var value = {};
var poisonedThen = Object.defineProperty({}, 'then', {
    get: function() {
        throw value;
    }
});
var promise = new Promise(function(resolve) {
    returnValue = resolve(poisonedThen);
});

assert.sameValue(returnValue, undefined);

promise
.then(
    v => $DONOTEVALUATE(),
    e => {
        assert.sameValue(e, value, 'The promise should be fulfilled with the provided value.');
})
.then($DONE, $DONE);
