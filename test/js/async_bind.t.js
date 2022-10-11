/*---
includes: [compareArray.js]
flags: [async]
---*/

async function f(a1, a2, a3) {
    var v = await a1;
    return [a1, a2, a3];
}

f.bind(null,1,2)('a')
.then(v => assert.compareArray(v, [1, 2, 'a']))
.then($DONE, $DONE);
