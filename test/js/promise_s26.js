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
        console.log('BoxedPromise.constructor');

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
        console.log('BoxedPromise.prototype.then');
        var rs = Object.create(Object.getPrototypeOf(this));
        rs.boxed = this.boxed.then(res, rej);
        return rs;
    };

    return BoxedPromise;
})();


var PatchedPromise = (() => {
    function PatchedPromise(executor) {
        console.log('PatchedPromise.constructor');

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
            .then((x) => console.log('resolved', name));

        console.log(name, 'resolve', resolved instanceof Class ? 'OK' : 'failed');


        var rejected = Class.reject(name)
            .catch((x) => console.log('rejected', name));

        console.log(name, 'reject', rejected instanceof Class ? 'OK' : 'failed');


        var instance = new Class((resolve) => {
            setImmediate(() => resolve(name));
        });

        var chain = instance
            .then((x) => { console.log('then', x); return x; })
            .then((x) => { console.log('then', x); return x; });

        console.log(name, 'chain', chain instanceof Class ? 'OK' : 'failed');

        var fin = chain
            .finally(() => console.log('finally', name));

        console.log(name, 'finally', fin instanceof Class ? 'OK' : 'failed');

        console.log(name, 'sync done\n');

        fin
            .then(() => console.log(name, 'async done\n'))
            .then(resolve);
    });
};

Promise.resolve()
    .then(() => testSubclass(BoxedPromise, 'BoxedPromise'))
    .then(() => testSubclass(PatchedPromise, 'PatchedPromise'));
