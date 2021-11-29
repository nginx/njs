/*---
includes: []
flags: [async]
---*/

let called = false;

var thenable = {
    then: function(resolve) {
        called = true;
        resolve();
    }
};

function executor(resolve, reject) {
    resolve(thenable);
    throw new Error('ignored');
}

new Promise(executor)
.then(() => assert.sameValue(called, true))
.then($DONE, $DONE);
