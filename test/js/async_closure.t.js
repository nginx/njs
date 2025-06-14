/*---
includes: []
flags: [async]
---*/

async function f() {
    await 1;
    var v = 2;

    function g() {
      return v + 1;
    }

    function s() {
      g + 1;
    }

    return g();
}

f().then(v => {
    assert.sameValue(v, 3)
})
.then($DONE, $DONE);
