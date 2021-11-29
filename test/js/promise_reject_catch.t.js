/*---
includes: []
flags: [async]
---*/

Promise.reject("test")
.catch(v => assert.sameValue(v, "test"))
.then($DONE, $DONE);
