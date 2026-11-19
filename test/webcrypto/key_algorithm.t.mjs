/*---
includes: [compatWebcrypto.js, runTsuite.js]
flags: [async]
---*/

async function test(params) {
    let key = await crypto.subtle.importKey('raw', new Uint8Array(16),
                                            params.name, false,
                                            ['deriveBits']);
    let algorithm = key.algorithm;

    if (Object.keys(algorithm).join() !== 'name'
        || algorithm.name !== params.name)
    {
        throw Error(`unexpected algorithm: ${JSON.stringify(algorithm)}`);
    }

    return 'SUCCESS';
}

let key_algorithm_tsuite = {
    name: 'derivation key algorithm',
    skip: () => !has_webcrypto(),
    T: test,
    tests: [
        { name: 'HKDF' },
        { name: 'PBKDF2' },
    ],
};

run([key_algorithm_tsuite])
.then($DONE, $DONE);
