/*---
includes: []
flags: [async]
---*/

let called = false;

Promise.resolve()
.then(() => {})
.catch(() => {})
.then(() => {nonExsisting()})
.catch(() => { called = true; })
.then(() => assert.sameValue(called, true))
.then($DONE, $DONE);
