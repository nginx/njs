/*---
includes: [compareArray.js]
flags: []
---*/

var res = [];
function abc() {
    var promise = new Promise(function(resolve, reject) {
        res.push('One');
        resolve();
    });
    res.push('Two');
    promise.then(() => {res.push('Four'); return {num: 'Five'}})
    .then((obj) => {res.push(obj.num);  return {num: 'Six'}})
    .then((obj) => {res.push(obj.num)});
    res.push('Three');
}

abc();
assert.compareArray(res, ['One', 'Two', 'Three']);
setTimeout(() => assert.compareArray(res, ['One', 'Two', 'Three', 'Four', 'Five', 'Six']), 0);
