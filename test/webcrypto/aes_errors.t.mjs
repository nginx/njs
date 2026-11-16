/*---
includes: [compatBuffer.js, compatWebcrypto.js, runTsuite.js]
flags: [async]
---*/

async function test(params) {
    let key = await crypto.subtle.importKey('raw', new Uint8Array(16),
                                            'AES-GCM', false,
                                            ['encrypt', 'decrypt']);
    let algorithm = {name: 'AES-GCM', iv: new Uint8Array(12)};
    let data = new Uint8Array(16);

    if (params.op == 'decrypt') {
        data = await crypto.subtle.encrypt(algorithm, key, data);
    }

    algorithm.additionalData = Symbol.toStringTag;

    try {
        await crypto.subtle[params.op](algorithm, key, data);

    } catch (e) {
        return 'SUCCESS';
    }

    throw Error('AES-GCM accepted invalid additionalData');
}

let aes_errors_tsuite = {
    name: 'AES-GCM additionalData conversion failure',
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: test,
    tests: [
        {op: 'encrypt'},
        {op: 'decrypt'},
    ],
};

run([aes_errors_tsuite])
.then($DONE, $DONE);
