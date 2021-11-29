/*---
includes: [compareArray.js]
flags: [async]
---*/

let stages = [];

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


var BoxedPromise = (() => {
    function BoxedPromise(executor) {
        stages.push('BoxedPromise.constructor');

        if (!(this instanceof BoxedPromise)) {
            return Promise(executor);
        }

        if (typeof executor !== 'function') {
            return new Promise(executor);
        }

        var context, args;
        var promise = new Promise(wrappedExecutor);
        this.boxed = promise;

        try {
            executor.apply(context, args);

        } catch (e) {
            args[1](e);
        }

        function wrappedExecutor(resolve, reject) {
            context = this;
            args = [wrappedResolve, wrappedReject];
            function wrappedResolve(val) {
                return resolve(val);
            }
            function wrappedReject(val) {
                return reject(val);
            }
        }
    }

    inherits(BoxedPromise, Promise);

    BoxedPromise.prototype.then = function(res, rej) {
        stages.push('BoxedPromise.prototype.then');
        var rs = Object.create(Object.getPrototypeOf(this));
        rs.boxed = this.boxed.then(res, rej);
        return rs;
    };

    return BoxedPromise;
})();


var PatchedPromise = (() => {
    function PatchedPromise(executor) {
        stages.push('PatchedPromise.constructor');

        if (!(this instanceof PatchedPromise)) {
            return Promise(executor);
        }

        if (typeof executor !== 'function') {
            return new Promise(executor);
        }

        var context, args;
        var promise = new Promise(wrappedExecutor);
        Object.setPrototypeOf(promise, PatchedPromise.prototype);

        try {
            executor.apply(context, args);

        } catch (e) {
            args[1](e);
        }

        return promise;

        function wrappedExecutor(resolve, reject) {
            context = this;
            args = [wrappedResolve, wrappedReject];
            function wrappedResolve(val) {
                return resolve(val);
            }
            function wrappedReject(val) {
                return reject(val);
            }
        }
    }

    inherits(PatchedPromise, Promise);

    return PatchedPromise;
})();


var testSubclass = (Class, name) => {
    return new Promise((resolve) => {
        var resolved = Class.resolve(name)
            .then((x) => stages.push(`resolved ${name}`));

        stages.push(`${name} resolve ${resolved instanceof Class ? 'OK' : 'failed'}`);


        var rejected = Class.reject(name)
            .catch((x) => stages.push(`rejected ${name}`));

        stages.push(`${name} reject ${rejected instanceof Class ? 'OK' : 'failed'}`);

        var instance = new Class((resolve) => {
            setImmediate(() => resolve(name));
        });

        var chain = instance
            .then((x) => { stages.push(`then ${x}`); return x; })
            .then((x) => { stages.push(`then ${x}`); return x; });

        stages.push(`${name} chain ${chain instanceof Class ? 'OK' : 'failed'}`);

        var fin = chain
            .finally(() => stages.push(`finally ${name}`));

        stages.push(`${name} finally ${fin instanceof Class ? 'OK' : 'failed'}`);

        stages.push(`${name} sync done`);

        fin
            .then(() => stages.push(`${name} async done`))
            .then(resolve);
    });
};

Promise.resolve()
.then(() => testSubclass(BoxedPromise, 'BoxedPromise'))
.then(() => {
    assert.compareArray(stages, [
      "BoxedPromise.constructor",
      "BoxedPromise.prototype.then",
      "BoxedPromise resolve OK",
      "BoxedPromise.constructor",
      "BoxedPromise.prototype.then",
      "BoxedPromise reject OK",
      "BoxedPromise.constructor",
      "BoxedPromise.prototype.then",
      "BoxedPromise.prototype.then",
      "BoxedPromise chain OK",
      "BoxedPromise.prototype.then",
      "BoxedPromise finally OK",
      "BoxedPromise sync done",

      "BoxedPromise.prototype.then",
      "BoxedPromise.prototype.then",
      "resolved BoxedPromise",
      "rejected BoxedPromise",
      "then BoxedPromise",
      "then BoxedPromise",
      "finally BoxedPromise",
      "BoxedPromise.constructor",
      "BoxedPromise.prototype.then",
      "BoxedPromise.prototype.then",
      "BoxedPromise async done",
    ]);
    stages = [];
})
.then(() => testSubclass(PatchedPromise, 'PatchedPromise'))
.then(() => {
    assert.compareArray(stages, [
        "PatchedPromise.constructor",
        "PatchedPromise.constructor",
        "PatchedPromise resolve OK",
        "PatchedPromise.constructor",
        "PatchedPromise.constructor",
        "PatchedPromise reject OK",
        "PatchedPromise.constructor",
        "PatchedPromise.constructor",
        "PatchedPromise.constructor",
        "PatchedPromise chain OK",
        "PatchedPromise.constructor",
        "PatchedPromise finally OK",
        "PatchedPromise sync done",

        "PatchedPromise.constructor",
        "PatchedPromise.constructor",
        "resolved PatchedPromise",
        "rejected PatchedPromise",
        "then PatchedPromise",
        "then PatchedPromise",
        "finally PatchedPromise",
        "PatchedPromise.constructor",
        "PatchedPromise.constructor",
        "PatchedPromise.constructor",
        "PatchedPromise async done",
    ]);
    stages = [];
})
.then($DONE, $DONE);
