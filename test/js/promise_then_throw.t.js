/*---
includes: []
flags: []
negative:
  phase: runtime
---*/

Promise.resolve()
.then(() => {nonExsisting()});
