/*---
includes: []
flags: [async]
---*/

Promise.resolve()
.finally(() => {nonExsistingInFinally()})
.catch(e => {})
.then($DONE, $DONE);
