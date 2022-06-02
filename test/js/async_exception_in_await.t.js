/*---
includes: []
flags: []
---*/

var p = new Promise(() => 0);
Object.defineProperty(p, "constructor", {get: () => ({}).a.a});

async function g() {
    try {
        await p;
    } catch (e) {
    }
}

function f() {
    g();

    return 42;
}

assert.sameValue(f(), 42);
