/*---
includes: []
flags: []
paths: [test/js/module, test/js/module/libs]
negative:
  phase: runtime
---*/

import fs from 'fs';
import lib from 'lib3.js';

fs.readFileSync({}.a.a);
