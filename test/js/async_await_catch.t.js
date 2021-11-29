/*---
includes: []
flags: [async]
---*/

async function add(x) {
    return await new Promise((resolve, reject) => {reject(x)}).catch(v => v + 1);
}

add(50).then(v => {
    assert.sameValue(v, 51);
})
.then($DONE, $DONE);
