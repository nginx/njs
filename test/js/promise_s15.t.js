/*---
includes: []
flags: [async]
---*/

var obj = {};
var p = Promise.resolve(obj);

p.then(undefined, undefined)
.then(
    v => {
        assert.sameValue(v, obj,
                         'Expected resolution object to be passed through, got ' + v);
    },
    e => $DONOTEVALUATE())
.then($DONE, $DONE);
