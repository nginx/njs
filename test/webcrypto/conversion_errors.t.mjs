/*---
includes: [compatWebcrypto.js, runTsuite.js]
flags: [async]
---*/

async function test(params) {
    let key = await crypto.subtle.importKey('raw', new Uint8Array(16),
                                            params.key, false, params.usage);
    let bad = {
        valueOf() { throw Error('marker'); },
        toString() { throw Error('marker'); },
    };

    try {
        await params.call(key, bad);

    } catch (e) {
        return 'SUCCESS';
    }

    throw Error('invalid parameter was accepted');
}

let data = new Uint8Array(16);
let salt = new Uint8Array(16);

let conversion_errors_tsuite = {
    name: 'algorithm parameter conversion failure',
    skip: () => !has_webcrypto(),
    T: test,
    prepare_args: (args) => args,
    tests: [
        { key: 'AES-GCM', usage: ['encrypt'],
          call: (key, bad) => crypto.subtle.encrypt(
              {name: 'AES-GCM', iv: bad}, key, data) },
        { key: 'AES-GCM', usage: ['encrypt'],
          call: (key, bad) => crypto.subtle.encrypt(
              {name: 'AES-GCM', iv: new Uint8Array(12), tagLength: bad},
              key, data) },
        { key: 'AES-GCM', usage: ['encrypt'],
          call: (key, bad) => crypto.subtle.encrypt(
              {name: 'AES-GCM', iv: new Uint8Array(12), additionalData: bad},
              key, data) },
        { key: 'AES-CTR', usage: ['encrypt'],
          call: (key, bad) => crypto.subtle.encrypt(
              {name: 'AES-CTR', counter: bad, length: 64}, key, data) },
        { key: 'AES-CTR', usage: ['encrypt'],
          call: (key, bad) => crypto.subtle.encrypt(
              {name: 'AES-CTR', counter: new Uint8Array(16), length: bad},
              key, data) },
        { key: 'AES-CBC', usage: ['encrypt'],
          call: (key, bad) => crypto.subtle.encrypt(
              {name: 'AES-CBC', iv: bad}, key, data) },
        { key: 'PBKDF2', usage: ['deriveKey'],
          call: (key, bad) => crypto.subtle.deriveKey(
              {name: 'PBKDF2', hash: 'SHA-256', salt, iterations: 1}, key,
              {name: 'AES-CBC', length: bad}, false, ['encrypt']) },
        { key: 'PBKDF2', usage: ['deriveBits'],
          call: (key, bad) => crypto.subtle.deriveBits(
              {name: 'PBKDF2', hash: 'SHA-256', salt, iterations: bad},
              key, 128) },
    ],
};

run([conversion_errors_tsuite])
.then($DONE, $DONE);
