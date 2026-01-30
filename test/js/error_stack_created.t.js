/*---
includes: []
flags: []
---*/

// Error.stack should be available when error is created (not just thrown)
// This matches V8, SpiderMonkey, and QuickJS behavior

function inner() {
    return new Error("test error");
}

function outer() {
    return inner();
}

var e = outer();

assert.sameValue(typeof e.stack, 'string');
assert.sameValue(e.stack.includes('inner'), true);
assert.sameValue(e.stack.includes('outer'), true);
