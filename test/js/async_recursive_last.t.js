/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

async function f(v) {
    if (v == 3) {
        return;
    }

    stages.push(`f>${v}`);

    f(v + 1);

    stages.push(`f<${v}`);

    await "X";
}

f(0)
.then(v => {
    assert.compareArray(stages, ['f>0', 'f>1', 'f>2', 'f<2', 'f<1', 'f<0']);
})
.then($DONE, $DONE);
