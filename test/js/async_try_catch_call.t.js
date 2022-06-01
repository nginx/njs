/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];
const fn = async () => { throw new Error('Oops') };

async function af() {
    try {
        await fn();

        $DONOTEVALUATE();
    }
    catch (v) {
        stages.push(`catch:${v}`);
    }
    finally {
        stages.push('finally');
    }

    return "end";
};

af().then(v => {
    stages.push(v);
    assert.compareArray(stages, ['catch:Error: Oops', 'finally', 'end']);
})
.then($DONE, $DONE)
