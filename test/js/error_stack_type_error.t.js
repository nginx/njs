/*---
includes: []
flags: []
---*/

// TypeError.stack should be available when error is created

function createTypeError() {
    return new TypeError("type error message");
}

var e = createTypeError();

assert.sameValue(typeof e.stack, 'string');
assert.sameValue(e.stack.includes('TypeError'), true);
assert.sameValue(e.stack.includes('createTypeError'), true);
