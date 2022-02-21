/*---
includes: [compareArray.js]
flags: []
paths: [test/js/module/]
---*/

if (!globalThis.stages) {
    globalThis.stages = [];
}

globalThis.stages.push('main1');

import _ from 'order.js';
import __ from 'order2.js';

globalThis.stages.push('main2');

assert.compareArray(globalThis.stages, ["order", "order2", "main1", "main2"]);
