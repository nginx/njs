/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

async function af() {
    try {
        await new Promise(function(resolve, reject) {
            reject("reject");
        });

        $DONOTEVALUATE();
    }
    catch (v) {
        stages.push(v);
    }
    finally {
        stages.push('finally');
    }

    return "end";
};

af().then(v => {
    stages.push(v);
    assert.compareArray(stages, ['reject', 'finally', 'end']);
})
.then($DONE, $DONE)
