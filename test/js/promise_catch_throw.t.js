/*---
includes: []
flags: [async]
---*/

Promise.resolve()
.then(() => {nonExsisting()})
.catch(() => {nonExsistingInCatch()})
.catch(v => assert.sameValue(v.constructor, ReferenceError))
.then($DONE, $DONE);
