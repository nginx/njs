/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

async function af() {
    try {
        throw "try";

        $DONOTEVALUATE();
    }
    finally {
        stages.push("finally");
    }

    return "shouldn't happen: end";
};

af()
.then(v => $DONOTEVALUATE())
.catch(v => {
    stages.push(v);
    assert.compareArray(stages, ['finally', 'try']);
})
.then($DONE, $DONE)
