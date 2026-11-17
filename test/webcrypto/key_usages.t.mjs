/*---
includes: [compatWebcrypto.js, runTsuite.js]
flags: [async]
---*/

async function test(params) {
    let marker = Error('keyUsages conversion');
    let usage = params.usage ? params.usage
                             : {toString() { throw marker; }};

    try {
        await crypto.subtle.importKey('raw', new Uint8Array(16), 'AES-GCM',
                                      false, ['encrypt', usage]);

    } catch (e) {
        if (params.usage ? !(e instanceof TypeError) : e !== marker) {
            throw Error(`unexpected keyUsages conversion error: ${e}`);
        }

        return 'SUCCESS';
    }

    throw Error('importKey accepted invalid keyUsages');
}

let key_usages_tsuite = {
    name: 'keyUsages conversion failure',
    skip: () => !has_webcrypto(),
    T: test,
    tests: [
        {},
        {usage: Symbol('usage')},
    ],
};

run([key_usages_tsuite])
.then($DONE, $DONE);
