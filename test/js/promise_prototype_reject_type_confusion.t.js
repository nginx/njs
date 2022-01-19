/*---
includes: []
flags: [async]
---*/

Symbol.__proto__ = new Promise(()=>{});

Promise.reject(Symbol)
.then(v => $DONOTEVALUATE())
.catch(err => assert.sameValue(err.name, 'Symbol'))
.then($DONE, $DONE);
