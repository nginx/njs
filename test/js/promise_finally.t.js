/*---
includes: []
flags: [async]
---*/

Promise.resolve('here')
.finally(() => {'nope'})
.then(v => assert.sameValue(v, 'here'));

Promise.resolve('here')
.finally(() => {throw 'nope'})
.then(v => $DONOTEVALUATE())
.catch(v => assert.sameValue(v, 'nope'))
.then($DONE, $DONE);
