/*---
includes: [compatBuffer.js, compatWebcrypto.js, runTsuite.js]
flags: [async]
---*/

async function test(params) {
    let key = await crypto.subtle.importKey('raw', new Uint8Array(16),
                                            'AES-CBC', true, ['encrypt']);
    let wrapping_key = await crypto.subtle.importKey('raw',
                                                     new Uint8Array(16),
                                                     'AES-KW', false,
                                                     ['wrapKey',
                                                      'unwrapKey']);

    try {
        switch (params.op) {
        case 'exportKey':
            await crypto.subtle.exportKey('JWK', key);
            break;

        case 'wrapKey':
            await crypto.subtle.wrapKey('JWK', key, wrapping_key, 'AES-KW');
            break;

        default:
            let wrapped = await crypto.subtle.wrapKey('raw', key,
                                                      wrapping_key,
                                                      'AES-KW');

            await crypto.subtle.unwrapKey('JWK', wrapped, wrapping_key,
                                          'AES-KW', 'AES-CBC', true,
                                          ['encrypt']);
        }

    } catch (e) {
        if (!(e instanceof TypeError) || !e.message.includes('JWK')) {
            throw Error(`unexpected ${params.op} error: ${e}`);
        }

        return 'SUCCESS';
    }

    throw Error(`${params.op} accepted unknown key format`);
}

let key_format_tsuite = {
    name: 'unknown key format',
    skip: async () => !await has_webcrypto_algorithm('AES-KW', 128),
    T: test,
    tests: [
        {op: 'exportKey'},
        {op: 'wrapKey'},
        {op: 'unwrapKey'},
    ],
};

run([key_format_tsuite])
.then($DONE, $DONE);
