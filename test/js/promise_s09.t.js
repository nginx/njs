/*---
includes: [compareArray.js]
flags: []
---*/

let calls = [];

function NotPromise(executor) {
  assert.sameValue(Object.isExtensible(executor), true);
  executor(function() {calls.push('S')}, function() {calls.push('R')});
}

Promise.resolve.call(NotPromise);

assert.compareArray(calls, ['S']);
