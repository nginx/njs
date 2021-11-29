/*---
includes: []
flags: [async]
---*/

let stages = [];

var isLoading = true;
var promise = new Promise((a, b) => {a()} );

promise.then(function(response) {
    throw new TypeError('oops');
})
.then(function(json) { })
.catch(e => stages.push(e))
.finally(() => stages.push('Done'))
.then(() => {
    assert.sameValue(stages[0] instanceof TypeError, true);
    assert.sameValue(stages[1], 'Done');
})
.then($DONE, $DONE);
