/*---
includes: []
flags: [async]
---*/

function pr(x) {
    return new Promise(resolve => {resolve(x)}).then(v => {throw v}).catch(v => v);
}

async function add(x) {
    const a = await pr(x);
    const b = await pr(x + 10);

    return a + b;
}

add(50).then(v => {
    assert.sameValue(v, 110);
})
.then($DONE, $DONE);
