/*---
includes: [compareArray.js]
flags: [async]
---*/

let stage = [];

async function f() {
    let sum = 0;

    stage.push(2);

    for (let x = 4; x < 14; x++) {
        sum += await new Promise((resolve, reject) => {resolve(x)});

        stage.push(x);
    }

    stage.push("end");

    return sum;
}

stage.push(1);

f().then(v => {
    assert.sameValue(v, 85);
    assert.compareArray(stage, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, "end"]);
}).then($DONE, $DONE);

stage.push(3);
