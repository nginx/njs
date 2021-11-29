/*---
includes: []
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
    finally {
        await new Promise(function(resolve, reject) {
            reject("finally reject");
        });

        $DONOTEVALUATE();
    }

    return "shouldn't happen: end";
};

af()
.then(v => $DONOTEVALUATE())
.catch(v => {
    assert.sameValue(v, "finally reject");
})
.then($DONE, $DONE);
