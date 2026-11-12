/*---
includes: []
flags: []
---*/

if (typeof njs == 'object' && njs.engine == 'QuickJS') {
    function f() { return 'f'; }

    assert.throws(TypeError, () => njs.on.call(f, 'exit', () => {}));
    assert.throws(TypeError, () => njs.on.call({}, 'exit', () => {}));
    assert.sameValue(f(), 'f');
}
