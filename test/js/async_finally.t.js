/*---
includes: []
flags: [async]
---*/

let called = false;
async function add(x) {
    return await new Promise((resolve, reject) => {reject(x + 1)})
                     .finally(() => {called = true});
}

add(50).catch(e => {
    assert.sameValue(e, 51);
    assert.sameValue(called, true, "finally was not invoked");
}).then($DONE, $DONE);
