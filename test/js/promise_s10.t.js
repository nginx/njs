/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

var promise = new Promise(function(resolve, reject) {
    stages.push('One');
    reject('Oh no');
});

stages.push('Two');

promise.then(() => {stages.push('Three')})
.catch((v) => {stages.push(v)})
.then(() => assert.compareArray(stages, ['One', 'Two', 'Three', 'Oh no']))
.then($DONE, $DONE);

stages.push('Three');
