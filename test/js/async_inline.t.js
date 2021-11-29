/*---
includes: []
flags: [async]
---*/

function pr(x) {
    return new Promise(resolve => {resolve(x)});
}

async function add() {
    const a = pr(20);
    const b = pr(50);
    return await a + await b;
}

add().then(v => {
    assert.sameValue(v, 70);
}).then($DONE, $DONE);
