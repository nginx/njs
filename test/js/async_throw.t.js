/*---
includes: []
flags: [async]
---*/

function pr(x) {
    return new Promise(resolve => {resolve(x)}).then(v => {throw v});
}

async function add(x) {
    const a = await pr(x);
    const b = await pr(x);

    return a + b;
}

add(50)
.then(v => $DONOTEVALUATE())
.catch(v => {
    assert.sameValue(v, 50);
})
.then($DONE, $DONE);
