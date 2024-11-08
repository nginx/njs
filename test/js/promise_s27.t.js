/*---
includes: []
flags: [async]
---*/

var inherits = (child, parent) => {
    child.prototype = Object.create(parent.prototype, {
        constructor: {
        value: child,
        enumerable: false,
        writable: true,
        configurable: true
        }
    });
    Object.setPrototypeOf(child, parent);
};

function BoxedPromise(executor) {
    var context, args;
    new Promise(wrappedExecutor);
    executor.apply(context, args);

    function wrappedExecutor(resolve, reject) {
        context = this;
        args = [v => resolve(v),v => reject(v)];
    }
}

inherits(BoxedPromise, Promise);

Promise.resolve()
.then(() => BoxedPromise.resolve())
.catch(e => assert.sameValue(e.constructor, TypeError))
.then($DONE, $DONE);
