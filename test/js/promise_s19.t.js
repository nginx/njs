/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

var returnValue = null;
var value = {};
var resolve;

var poisonedThen = Object.defineProperty({}, 'then', {
    get: function() {
        stages.push('Throw!');
        throw value;
    }
});

var promise = new Promise(function(_resolve) {
    resolve = _resolve;
});

promise.then(
    v => $DONOTEVALUATE(),
    e => {
        stages.push('Reject!');
        assert.sameValue(e, value, 'The promise should be fulfilled with the provided value.');
})
.then(() => assert.compareArray(stages, ['Throw!', undefined, 'Reject!']))
.then($DONE, $DONE);

returnValue = resolve(poisonedThen);

stages.push(returnValue);
