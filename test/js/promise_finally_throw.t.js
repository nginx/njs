/*---
includes: []
flags: []
negative:
  phase: runtime
---*/

Promise.resolve()
.finally(() => {nonExsistingInFinally()});
