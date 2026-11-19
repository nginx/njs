/*---
includes: [compatNjs.js, compatBuffer.js, compatWebcrypto.js, compareArray.js, runTsuite.js]
flags: [async]
---*/

/*
 * Each test runs "op" where a getter or valueOf() (with "coerce") of
 * "trigger" applies "effect" to the buffer at "target":
 *   "detach"    - detaches the buffer (default),
 *   "resize"    - shrinks a resizable buffer to zero,
 *   "fill"      - changes the buffer contents,
 *   "temporary" - the target getter returns a new buffer each time,
 *   "drop"      - the target getter returns the last reference.
 *
 * The outcome is compared with the same operation where the target is:
 *   "empty"   - an empty byte sequence (the bytes were taken after
 *               the trigger ran),
 *   "copied"  - the original bytes (the bytes were copied before
 *               the trigger ran),
 *   "mutated" - the changed bytes (the bytes were taken after
 *               the trigger ran).
 */

function bytes(length, value) {
    return new Uint8Array(length).fill(value);
}

function locate(args, path) {
    let names = path.split('.');
    let holder = args;

    for (let i = 0; i < names.length - 1; i++) {
        holder = holder[names[i]];
    }

    return { holder, name: names[names.length - 1] };
}

function invalidate(buffer) {
    if (is_detach_available()) {
        detach(buffer);

    } else {
        buffer.transfer();
    }
}

function has_invalidate() {
    return is_detach_available()
           || typeof ArrayBuffer.prototype.transfer == 'function';
}

function has_resize() {
    return typeof ArrayBuffer.prototype.resize == 'function';
}

function cipher_algorithm(name) {
    switch (name) {
    case 'AES-GCM':
        return { name, additionalData: bytes(17, 2), iv: bytes(12, 3),
                 tagLength: 128 };

    case 'AES-CTR':
        return { name, counter: bytes(16, 3), length: 64 };

    default:
        return { name, iv: bytes(16, 3) };
    }
}

function derive_algorithm(name) {
    if (name == 'PBKDF2') {
        return { name, hash: 'SHA-256', iterations: 1, salt: bytes(16, 5) };
    }

    return { name, hash: 'SHA-256', info: bytes(8, 6), salt: bytes(16, 5) };
}

function sign_algorithm(name) {
    switch (name) {
    case 'ECDSA':
        return { name, hash: 'SHA-256' };

    case 'RSA-PSS':
        return { name, saltLength: 16 };

    default:
        return { name };
    }
}

function import_algorithm(name) {
    return (name == 'ECDSA') ? { name, namedCurve: 'P-256' }
                             : { name, hash: 'SHA-256' };
}

async function sign_keys(name) {
    switch (name) {
    case 'ECDSA':
        return crypto.subtle.generateKey({ name, namedCurve: 'P-256' }, true,
                                         ['sign', 'verify']);

    case 'RSA-PSS':
        return crypto.subtle.generateKey({ name, modulusLength: 1024,
                                    publicExponent: new Uint8Array([1, 0, 1]),
                                    hash: 'SHA-256' },
                                         true, ['sign', 'verify']);

    default:
        let key = await crypto.subtle.importKey('raw', bytes(16, 1),
                                                { name, hash: 'SHA-256' },
                                                false, ['sign', 'verify']);

        return { privateKey: key, publicKey: key };
    }
}

function cipher_op(op) {
    return {
        keys: p => crypto.subtle.importKey('raw', bytes(16, 1), p.alg, false,
                                           [op]),
        args: p => ({ algorithm: cipher_algorithm(p.alg),
                      data: bytes(32, 4) }),
        run: (k, a) => crypto.subtle[op](a.algorithm, k, a.data),
    };
}

let ops = {
    encrypt: cipher_op('encrypt'),
    decrypt: cipher_op('decrypt'),

    digest: {
        keys: () => null,
        args: () => ({ algorithm: 'SHA-256', data: bytes(32, 4) }),
        run: (k, a) => crypto.subtle.digest(a.algorithm, a.data),
    },

    deriveBits: {
        keys: async p => {
            if (p.alg == 'ECDH') {
                let alg = { name: 'ECDH', namedCurve: 'P-256' };
                let pair = await crypto.subtle.generateKey(alg, true,
                                                           ['deriveBits']);
                let raw = await crypto.subtle.exportKey('raw', pair.publicKey);

                return { alg, raw, key: pair.privateKey, length: 256 };
            }

            let key = await crypto.subtle.importKey('raw', bytes(16, 1), p.alg,
                                                    false, ['deriveBits']);

            return { key, length: 128 };
        },
        args: async (p, k) => {
            if (p.alg == 'ECDH') {
                let pub = await crypto.subtle.importKey('raw', k.raw, k.alg,
                                                        true, []);

                return { algorithm: { name: 'ECDH', public: pub } };
            }

            return { algorithm: derive_algorithm(p.alg) };
        },
        run: (k, a) => crypto.subtle.deriveBits(a.algorithm, k.key, k.length),
    },

    importKey: {
        keys: async p => {
            if (p.alg == 'HMAC') {
                return bytes(16, 8);
            }

            let pair = await sign_keys(p.alg);

            return new Uint8Array(await crypto.subtle.exportKey(p.format,
                                                              pair.publicKey));
        },
        args: (p, k) => ({ algorithm: import_algorithm(p.alg),
                           keyData: k.slice() }),
        run: async (k, a, p) => {
            let usages = (p.alg == 'HMAC') ? ['sign'] : ['verify'];
            let key = await crypto.subtle.importKey(p.format, a.keyData,
                                                    a.algorithm, true, usages);

            return crypto.subtle.exportKey(p.format, key);
        },
    },

    sign: {
        keys: p => sign_keys(p.alg),
        args: p => ({ algorithm: sign_algorithm(p.alg), data: bytes(32, 4) }),
        run: (k, a) => crypto.subtle.sign(a.algorithm, k.privateKey, a.data),

        /* Signatures may be randomized, so they are verified instead. */
        compare: (k, actual, expected, a, p) =>
            crypto.subtle.verify(sign_algorithm(p.alg), k.publicKey, actual,
                                 a.data),
    },

    verify: {
        keys: async p => {
            let keys = await sign_keys(p.alg);
            let sig = await crypto.subtle.sign(sign_algorithm(p.alg),
                                               keys.privateKey, bytes(32, 4));

            keys.signature = new Uint8Array(sig);

            return keys;
        },
        args: (p, k) => ({ algorithm: sign_algorithm(p.alg),
                           signature: k.signature.slice(),
                           data: bytes(32, 4) }),
        run: (k, a) => crypto.subtle.verify(a.algorithm, k.publicKey,
                                            a.signature, a.data),
    },

    unwrapKey: {
        keys: async p => {
            let alg = (p.alg == 'AES-GCM') ? { name: p.alg, iv: bytes(12, 3) }
                                           : { name: p.alg };
            let wrapping = await crypto.subtle.importKey('raw', bytes(16, 1),
                                                         p.alg, false,
                                                         ['wrapKey',
                                                          'unwrapKey']);
            let key = await crypto.subtle.importKey('raw', bytes(16, 9),
                                                    'AES-CBC', true,
                                                    ['encrypt']);
            let wrapped = await crypto.subtle.wrapKey('raw', key, wrapping,
                                                      alg);

            return { alg, wrapping, wrapped: new Uint8Array(wrapped) };
        },
        args: (p, k) => ({ wrappedKey: k.wrapped.slice(),
                           unwrapAlgorithm: Object.assign({}, k.alg),
                           unwrappedKeyAlgorithm: { name: 'AES-CBC' },
                           keyUsages: ['encrypt'] }),
        run: async (k, a) => {
            let key = await crypto.subtle.unwrapKey('raw', a.wrappedKey,
                                                    k.wrapping,
                                                    a.unwrapAlgorithm,
                                                    a.unwrappedKeyAlgorithm,
                                                    true, a.keyUsages);

            return crypto.subtle.exportKey('raw', key);
        },
    },
};

let keys_cache = {};

function op_keys(p) {
    let id = `${p.op}:${p.alg}:${p.format}`;

    if (!keys_cache[id]) {
        keys_cache[id] = ops[p.op].keys(p);
    }

    return keys_cache[id];
}

function set_trigger(args, p, effect) {
    if (!p.trigger) {
        effect();
        return;
    }

    let t = locate(args, p.trigger);
    let value = t.holder[t.name];

    if (p.coerce) {
        t.holder[t.name] = { valueOf() { effect(); return value; } };
        return;
    }

    Object.defineProperty(t.holder, t.name, {
        get() { effect(); return value; },
        enumerable: true,
        configurable: true,
    });
}

async function prepare(args, p) {
    let t = locate(args, p.target);
    let value = t.holder[t.name];

    switch (p.effect) {
    case 'temporary':
        /* No other reference to the returned buffer is kept. */

        Object.defineProperty(t.holder, t.name, {
            get() { return value.slice(); },
            enumerable: true,
        });

        return;

    case 'drop':
        /* The getter returns the last reference to the object. */

        let box = { value };

        Object.defineProperty(t.holder, t.name, {
            get() {
                let v = box.value;

                box.value = null;

                if (typeof $262 != 'undefined' && $262.gc) {
                    $262.gc();
                }

                return v;
            },
        });

        value = null;

        /* Drop the references the async frames keep to awaited values. */

        for (let i = 0; i < 5; i++) {
            await null;
        }

        return;

    case 'resize':
        let rab = new ArrayBuffer(value.length,
                                  { maxByteLength: 2 * value.length });
        let view = new Uint8Array(rab);

        view.set(value);
        t.holder[t.name] = view;

        set_trigger(args, p, () => rab.resize(0));
        return;

    case 'fill':
        set_trigger(args, p, () => value.fill(7));
        return;

    default:
        /* "detach". */

        set_trigger(args, p, () => invalidate(value.buffer));
        return;
    }
}

function expect_args(args, p) {
    let t = locate(args, p.target);

    switch (p.expect) {
    case 'empty':
        t.holder[t.name] = new Uint8Array(0);
        break;

    case 'mutated':
        t.holder[t.name].fill(7);
        break;

    default:
        /* "copied". */
        break;
    }

    return args;
}

async function outcome(promise) {
    try {
        let value = await promise;

        return { ok: true,
                 value: (typeof value == 'boolean') ? value
                                                    : new Uint8Array(value) };

    } catch (e) {
        return { ok: false, error: e };
    }
}

async function test(p) {
    let op = ops[p.op];
    let k = await op_keys(p);
    let args = await op.args(p, k);
    let eargs = expect_args(await op.args(p, k), p);

    await prepare(args, p);

    let actual = await outcome(op.run(k, args, p));
    let expected = await outcome(op.run(k, eargs, p));

    if (actual.ok !== expected.ok) {
        throw Error(`${actual.ok ? 'resolved' : actual.error} vs `
                    + `${expected.ok ? 'resolved' : expected.error}`);
    }

    if (!actual.ok) {
        return 'SUCCESS';
    }

    if (op.compare) {
        if (!await op.compare(k, actual.value, expected.value, eargs, p)) {
            throw Error('unexpected result');
        }

    } else if (typeof expected.value == 'boolean'
               ? actual.value !== expected.value
               : !compareArray(actual.value, expected.value))
    {
        throw Error(`unexpected result ${actual.value}`);
    }

    return 'SUCCESS';
}

/* Node.js reads BufferSource members lazily and rejects detached ones. */

let njs_only = () => !has_njs();

let suite = {
    skip: () => (!has_webcrypto()
                 || !has_invalidate()
                 || (has_njs() && njs.engine != 'QuickJS')),
    T: test,
};

let cipher_tsuite = Object.assign({}, suite, {
    name: "cipher buffer lifetime",
    opts: { op: 'encrypt' },

    tests: [
        { alg: 'AES-GCM', target: 'data', trigger: 'algorithm.tagLength',
          expect: 'empty' },
        { alg: 'AES-GCM', target: 'data', trigger: 'algorithm.tagLength',
          coerce: true, expect: 'empty' },
        { alg: 'AES-CTR', target: 'data', trigger: 'algorithm.length',
          coerce: true, expect: 'empty' },
        { alg: 'AES-CBC', target: 'data', trigger: 'algorithm.iv',
          expect: 'empty' },
        { alg: 'AES-CBC', target: 'data', trigger: 'algorithm.iv',
          effect: 'resize', expect: 'empty', skip: () => !has_resize() },
        { op: 'decrypt', alg: 'AES-CTR', target: 'data',
          trigger: 'algorithm.length', expect: 'empty' },

        { alg: 'AES-GCM', target: 'algorithm.iv',
          trigger: 'algorithm.tagLength', expect: 'copied', skip: njs_only },
        { alg: 'AES-GCM', target: 'algorithm.iv',
          trigger: 'algorithm.tagLength', effect: 'fill', expect: 'copied',
          skip: njs_only },
        { alg: 'AES-GCM', target: 'algorithm.additionalData',
          trigger: 'algorithm.iv', effect: 'fill', expect: 'copied',
          skip: njs_only },
        { alg: 'AES-GCM', target: 'algorithm.iv',
          trigger: 'algorithm.additionalData', effect: 'fill',
          expect: 'mutated' },
        { alg: 'AES-CTR', target: 'algorithm.counter',
          trigger: 'algorithm.length', expect: 'copied', skip: njs_only },
        { alg: 'AES-GCM', target: 'algorithm.iv', effect: 'temporary',
          expect: 'copied' },

        { op: 'digest', target: 'data', expect: 'empty' },
    ],
});

let derive_tsuite = Object.assign({}, suite, {
    name: "derive buffer lifetime",
    opts: { op: 'deriveBits' },

    tests: [
        { alg: 'PBKDF2', target: 'algorithm.salt',
          trigger: 'algorithm.iterations', coerce: true, expect: 'empty',
          skip: njs_only },
        { alg: 'HKDF', target: 'algorithm.salt', trigger: 'algorithm.info',
          expect: 'empty', skip: njs_only },
        { alg: 'HKDF', target: 'algorithm.info', trigger: 'algorithm.salt',
          expect: 'copied', skip: njs_only },
        { alg: 'HKDF', target: 'algorithm.salt', effect: 'temporary',
          expect: 'copied' },
        { alg: 'ECDH', target: 'algorithm.public', effect: 'drop',
          expect: 'copied' },
    ],
});

let import_tsuite = Object.assign({}, suite, {
    name: "importKey buffer lifetime",
    opts: { op: 'importKey', format: 'raw', target: 'keyData',
            expect: 'empty' },

    tests: [
        { alg: 'HMAC', trigger: 'algorithm.hash' },
        { alg: 'ECDSA', trigger: 'algorithm.namedCurve' },
        { alg: 'ECDSA', format: 'spki', trigger: 'algorithm.namedCurve' },
        { alg: 'RSA-PSS', format: 'spki', trigger: 'algorithm.hash' },
    ],
});

let sign_tsuite = Object.assign({}, suite, {
    name: "sign and verify buffer lifetime",
    opts: { op: 'sign', target: 'data', expect: 'empty' },

    tests: [
        { alg: 'HMAC', trigger: 'algorithm.name' },
        { alg: 'ECDSA', trigger: 'algorithm.hash' },
        { alg: 'RSA-PSS', trigger: 'algorithm.saltLength', coerce: true },

        { op: 'verify', alg: 'HMAC', trigger: 'algorithm.name' },
        { op: 'verify', alg: 'HMAC', target: 'signature',
          trigger: 'algorithm.name' },
        { op: 'verify', alg: 'ECDSA', trigger: 'algorithm.hash' },
        { op: 'verify', alg: 'RSA-PSS', target: 'signature',
          trigger: 'algorithm.saltLength', coerce: true },
    ],
});

let unwrap_tsuite = Object.assign({}, suite, {
    name: "unwrapKey buffer lifetime",
    opts: { op: 'unwrapKey', alg: 'AES-GCM', target: 'wrappedKey',
            expect: 'empty' },

    tests: [
        { trigger: 'unwrapAlgorithm.name' },
        { trigger: 'unwrappedKeyAlgorithm.name' },
        { trigger: 'keyUsages.0' },
        { alg: 'AES-KW', trigger: 'keyUsages.0',
          skip: async () => !await has_webcrypto_algorithm('AES-KW') },
    ],
});

run([
    cipher_tsuite,
    derive_tsuite,
    import_tsuite,
    sign_tsuite,
    unwrap_tsuite,
])
.then($DONE, $DONE);
