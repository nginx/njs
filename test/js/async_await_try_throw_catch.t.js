/*---
includes: [compareArray.js]
flags: [async]
---*/

let stage = [];

async function af() {
    try {
        throw "try";

        $DONOTEVALUATE();
    }
    catch (v) {
        stage.push(v);
    }
    finally {
        stage.push("finally");
    }

    return "end";
};

af().then(v => {
    stage.push(v);
    assert.compareArray(stage, ['try', 'finally', 'end'])
}).then($DONE, $DONE);
