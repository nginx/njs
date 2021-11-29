/*---
includes: []
flags: [async]
---*/

let called = false;

Promise.resolve('Success')
.then(
    function(value) {
        assert.sameValue(value, 'Success');
        called = true;
    },
    function(value) {
        $DONOTEVALUATE()
})
.then(() => assert.sameValue(called, true))
.then($DONE, $DONE);
