/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

function pr(x) { return new Promise(resolve => {resolve(x)}); }

pr(10)
.then(async (v) => {
    stages.push("then before");
    let y = await pr(22);
    stages.push(`then ${v} ${y}`);
    return v + y;
})
.then(v => stages.push(`then2 ${v}`))
.catch(e => $DONOTEVALUATE())
.then(v => assert.compareArray(stages, ['then before', 'then 10 22', 'then2 32']))
.then($DONE, $DONE);
