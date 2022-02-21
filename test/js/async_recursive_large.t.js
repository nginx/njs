/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

async function f(v) {
    if (v == 1000) {
        return;
    }

    stages.push(`f>${v}`);

    await "X";

    await f(v + 1);

    stages.push(`f<${v}`);
}

f(0)
.then(v => {
    assert.sameValue(stages.length, 2000);
})
.then($DONE, $DONE);
