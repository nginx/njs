/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

async function af() {
    try {
        await ({}).a.a();

        $DONOTEVALUATE();
    }
    catch (v) {
        stages.push('catch');
    }
    finally {
        stages.push('finally');
    }

    return "end";
};

af().then(v => {
    stages.push(v);
    assert.compareArray(stages, ['catch', 'finally', 'end']);
})
.then($DONE, $DONE)
