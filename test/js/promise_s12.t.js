/*---
includes: []
flags: [async]
---*/

let called = false;

var thenable = new Promise(function() {});

var p = new Promise(function(resolve) {
    resolve();
    throw thenable;
});

p.then(function() {
    called = true;
})
.then(() => assert.sameValue(called, true))
.then($DONE, $DONE);
