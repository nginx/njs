/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

var thenable = {
    then: function(resolve) {
        resolve();
        stages.push(5);
    }
};

var thenableWithError = {
    then: function(resolve) {
        stages.push(3);
        resolve(thenable);
        stages.push(4);
        throw new Error('ignored exception');
    }
};

function executor(resolve, reject) {
    stages.push(1);
    resolve(thenableWithError);
    stages.push(2);
}

new Promise(executor)
.then(() => assert.compareArray(stages, [1, 2, 3, 4, 5]))
.then($DONE, $DONE);
