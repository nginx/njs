/*---
includes: []
flags: []
negative:
  phase: runtime
---*/

Promise.resolve()
.then(() => {nonExsistingOne()});

Promise.resolve()
.then(() => {nonExsistingTwo()});
