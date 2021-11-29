/*---
includes: []
flags: [async]
---*/

var obj = {};
var p = Promise.reject(obj);

p.then(undefined, undefined)
.then(
    v => DONOTEVALUATE(),
    e => {
        assert.sameValue(e, obj,
                         'Expected resolution object to be passed through, got ' + e);
})
.then($DONE, $DONE);
