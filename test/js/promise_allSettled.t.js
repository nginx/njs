/*---
includes: []
flags: [async]
---*/

var p0 = Promise.resolve(2).then(v => v + 1);
var p1 = Promise.reject(21).catch(v => v * 2);
var p2 = Promise.resolve('nope').then(() => { throw 'foo' });
var p3 = Promise.reject('yes').then(() => { throw 'nope'; });
var p4 = Promise.resolve('here').finally(() => 'nope');
var p5 = Promise.reject('here too').finally(() => 'nope');
var p6 = Promise.resolve('nope').finally(() => { throw 'finally'; });
var p7 = Promise.reject('nope').finally(() => { throw 'finally after rejected'; });
var p8 = Promise.reject(1).then(() => 'nope', () => 0);

function dump(v) {
    var fulfilled = v.filter(v=>v.status == 'fulfilled').map(v=>v.value).sort();
    var rejected = v.filter(v=>v.status == 'rejected').map(v=>v.reason).sort();
    return `F:${fulfilled}|R:${rejected}`
}

Promise.allSettled([p0, p1, p2, p3, p4, p5, p6, p7, p8]).then(
    v => assert.sameValue(dump(v), "F:0,3,42,here|R:finally,finally after rejected,foo,here too,yes"),
    v => $DONOTEVALUATE(),
)
.then($DONE, $DONE);
