/*---
includes: []
flags: [async]
---*/

var value = {};
var resolve;
var poisonedThen = Object.defineProperty({}, 'then', {
    get: function() {
        throw value;
    }
});

var p1 = new Promise(function(_resolve) {
    resolve = _resolve;
});

var p2;

p2 = p1.then(function() {
    return poisonedThen;
});

p2
.then(
    v => $DONOTEVALUATE(),
    e =>  {
        assert.sameValue(e, value, 'The promise should be rejected with the thrown exception.');
})
.then($DONE, $DONE);

resolve();
