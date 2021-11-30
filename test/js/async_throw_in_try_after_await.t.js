/*---
includes: []
flags: [async]
---*/

function pr(x) {
    return new Promise(resolve => {resolve(x)});
}

async function add(x) {
    try {
        const a = await pr(x);
        throw 'Oops';
        return a + b;

    } catch (e) {
        return `catch: ${e.toString()}`;
    }
}

add(50)
.then(
    v => assert.sameValue(v, 'catch: Oops'),
    v => $DONOTEVALUATE(),
).then($DONE, $DONE);
