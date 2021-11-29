/*---
includes: []
flags: [async]
---*/

async function af(x) {
    return x;
}

af(12345).then(v => {
    assert.sameValue(v, 12345)
})
.then($DONE, $DONE);
