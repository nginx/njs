/*---
includes: []
flags: [async]
---*/

// Error.stack should be available when error is passed to reject()

function rejectWithError() {
    return new Promise((resolve, reject) => {
        reject(new Error("rejected"));
    });
}

rejectWithError()
.catch(e => {
    assert.sameValue(typeof e.stack, 'string');
    assert.sameValue(e.stack.includes('Error'), true);
})
.then($DONE, $DONE);
