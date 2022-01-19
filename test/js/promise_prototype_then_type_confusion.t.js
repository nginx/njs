/*---
includes: []
flags: [async]
---*/

Symbol.__proto__ = new Promise(()=>{});

Promise.resolve(Symbol)
.then(v => $DONOTEVALUATE())
.catch(err => assert.sameValue(err.name, 'TypeError'))
.then($DONE, $DONE);
