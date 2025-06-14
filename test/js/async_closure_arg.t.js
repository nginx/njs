/*---
includes: []
flags: [async]
---*/

async function f(v) {
    await 1;
    v = 2;

    function g() {
      return v + 1;
    }

    function s() {
      g + 1;
    }

    return g();
}

f(42).then(v => {
    assert.sameValue(v, 3)
})
.then($DONE, $DONE);
