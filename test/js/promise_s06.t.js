/*---
includes: []
flags: [async]
---*/

var p = Promise.resolve([1,2,3]);
p.then(function(v) {
    assert.sameValue(v[0], 1);
})
.then($DONE, $DONE);
