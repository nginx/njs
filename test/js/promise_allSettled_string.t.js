/*---
includes: []
flags: [async]
---*/

function dump(v) {
    var fulfilled = v.filter(v=>v.status == 'fulfilled').map(v=>v.value).sort();
    var rejected = v.filter(v=>v.status == 'rejected').map(v=>v.reason).sort();
    return `F:${fulfilled}|R:${rejected}`;
}

Promise.allSettled("abc").then(
    v => assert.sameValue(dump(v), "F:a,b,c|R:"),
    v => $DONOTEVALUATE(),
)
.then($DONE, $DONE);
