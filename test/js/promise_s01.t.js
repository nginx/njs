/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

var promise = new Promise(function(resolve, reject) {
    stages.push('One');

    reject(123);
}).catch((v) => {stages.push(v)});

stages.push('Two');

promise.then(() => {stages.push('Four'); return {num: 'Five'}})
.then(obj => {stages.push(obj.num);  return {num: 'Six'}})
.then(obj => {stages.push(obj.num)})
.then(() => assert.compareArray(stages, ["One", "Two", "Three", 123, "Four", "Five", "Six"]))
.then($DONE, $DONE);

stages.push('Three');
