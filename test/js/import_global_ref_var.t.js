/*---
includes: []
flags: []
paths: [test/js/module/]
---*/

globalThis.a = 42;
import m from 'export_global_a.js';

assert.sameValue(m.f(), 42);
