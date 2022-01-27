/*---
includes: []
flags: []
paths: [test/js/module, test/js/module/libs]
negative:
  phase: runtime
---*/

import lib from 'lib3.js';

lib.exception();
