/*---
includes: []
flags: [async]
---*/

async function f() {
    await 1;
    var v = 'f';

    function g() {
        v += ':g';
        return v;
    }

    function s() {
        v += ':s';
        return v;
    }

    return [g, s];
}

f().then(pair => {
    pair[0]();
    var v = pair[1]();
    assert.sameValue(v, 'f:g:s');
})
.then($DONE, $DONE);
