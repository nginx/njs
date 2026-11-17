/*---
includes: [compatWebcrypto.js, runTsuite.js]
flags: [async]
---*/

async function test(params) {
    let promise;
    let key = await crypto.subtle.importKey('raw', new Uint8Array(16),
                                            'AES-GCM', false,
                                            ['encrypt', 'decrypt']);

    try {
        promise = params.call(key);

    } catch (e) {
        throw Error(`${params.name} threw synchronously: ${e}`);
    }

    if (!(promise instanceof Promise)) {
        throw Error(`${params.name} did not return a Promise`);
    }

    return promise.then(() => {
        throw Error(`${params.name} unexpectedly resolved`);
    }, () => 'SUCCESS');
}

let promise_tsuite = {
    name: 'SubtleCrypto rejects invalid arguments',
    skip: () => !has_webcrypto(),
    T: test,
    prepare_args: (args) => args,
    tests: [
        { name: 'decrypt',
          call: () => crypto.subtle.decrypt('AES-GCM', {},
                                            new Uint8Array(16)) },
        { name: 'decrypt data',
          call: (key) => crypto.subtle.decrypt({name: 'AES-GCM',
                                                iv: new Uint8Array(12)},
                                               key, Symbol.toStringTag) },
        { name: 'deriveBits',
          call: () => crypto.subtle.deriveBits('HKDF', {}, 128) },
        { name: 'deriveKey',
          call: () => crypto.subtle.deriveKey('HKDF', {},
                                              {name: 'AES-CBC', length: 128},
                                              false, ['encrypt']) },
        { name: 'digest',
          call: () => crypto.subtle.digest('XXX', new Uint8Array(16)) },
        { name: 'encrypt',
          call: () => crypto.subtle.encrypt('AES-GCM', {},
                                            new Uint8Array(16)) },
        { name: 'encrypt data',
          call: (key) => crypto.subtle.encrypt({name: 'AES-GCM',
                                                iv: new Uint8Array(12)},
                                               key, Symbol.toStringTag) },
        { name: 'exportKey',
          call: () => crypto.subtle.exportKey('XXX', {}) },
        { name: 'generateKey',
          call: () => crypto.subtle.generateKey('XXX', false,
                                                ['encrypt']) },
        { name: 'importKey',
          call: () => crypto.subtle.importKey('XXX', new Uint8Array(16),
                                              'AES-GCM', false,
                                              ['encrypt']) },
        { name: 'sign',
          call: () => crypto.subtle.sign('HMAC', {}, new Uint8Array(16)) },
        { name: 'unwrapKey',
          call: () => crypto.subtle.unwrapKey('raw', Symbol.toStringTag,
                                              {}, 'AES-KW', 'AES-CBC',
                                              false, ['encrypt']) },
        { name: 'verify',
          call: () => crypto.subtle.verify('HMAC', {}, new Uint8Array(16),
                                           new Uint8Array(16)) },
        { name: 'wrapKey',
          call: () => crypto.subtle.wrapKey('raw', {}, {}, 'AES-KW') },
    ],
};

run([promise_tsuite])
.then($DONE, $DONE);
