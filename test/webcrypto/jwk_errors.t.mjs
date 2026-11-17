/*---
includes: [compatFs.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    let jwk = load_jwk(params.jwk);

    jwk.key_ops = ['foo'];

    try {
        await crypto.subtle.importKey('jwk', jwk, params.alg, true,
                                      ['verify']);

    } catch (e) {
        if (e.message.startsWith('Invalid JWK')) {
            throw Error(`key_ops error was replaced: ${e}`);
        }

        return 'SUCCESS';
    }

    throw Error('invalid key_ops was accepted');
}

let jwk_errors_tsuite = {
    name: 'JWK import error reporting',
    skip: () => !has_webcrypto(),
    T: test,
    prepare_args: (args) => args,
    tests: [
        { jwk: 'rsa.pub.jwk', alg: {name: 'RSA-PSS', hash: 'SHA-256'} },
        { jwk: 'ec.pub.jwk', alg: {name: 'ECDSA', namedCurve: 'P-256'} },
    ],
};

run([jwk_errors_tsuite])
.then($DONE, $DONE);
