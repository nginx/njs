/*---
includes: []
flags: [async]
---*/

async function add(x) {
    return await new Promise((resolve, reject) => {reject(x)});
}

add(50)
.then(v => $DONOTEVALUATE())
.catch(v => {
    assert.sameValue(v, 50);
})
.then($DONE, $DONE);
