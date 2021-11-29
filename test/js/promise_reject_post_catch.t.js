/*---
includes: []
flags: []
negative:
  phase: runtime
---*/

var p = Promise.reject();
setImmediate(() => {p.catch(() => {})});
