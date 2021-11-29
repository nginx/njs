/*---
includes: []
flags: []
negative:
  phase: runtime
---*/

Promise.reject(new Error('Oh my'))
.then(
    function(success) {},
    function(error) { throw error;}
);
