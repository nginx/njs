/*---
includes: []
flags: [async]
---*/

var p1 = Promise.resolve({
    then: function(onFulfill, onReject) { onFulfill('fulfilled!'); }
});

p1.then(
    function(v) { assert.sameValue(v, 'fulfilled!'); },
    function(e) { $DONOTEVALUATE() },
)
.then(() => assert.sameValue(p1 instanceof Promise, true))
.then($DONE, $DONE);
