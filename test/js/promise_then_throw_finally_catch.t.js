/*---
includes: []
flags: [async]
---*/

let called = false;

Promise.resolve()
.then(() => {nonExsisting()})
.finally(() => {})
.catch(() => {called = true;})
.then(v => assert.sameValue(called, true))
.then($DONE, $DONE);
