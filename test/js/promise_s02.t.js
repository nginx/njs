/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

var promise = new Promise(function(resolve, reject) {
    stages.push('One');

    reject(123);
})

stages.push('Two');

promise.then(() => {stages.push('Four'); return {num: 'Five'}})
.then(obj => {stages.push(obj.num);  return {num: 'Six'}})
.then(obj => {stages.push(obj.num)})
.catch(v => assert.sameValue(v, 123))
.then(() => assert.compareArray(stages, ["One", "Two", "Three"]))
.then($DONE, $DONE);

stages.push('Three');
