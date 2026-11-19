/*---
includes: [compatBuffer.js, compatWebcrypto.js, runTsuite.js]
flags: [async]
---*/

/*
 * Algorithm members returned by getters may be the only reference to
 * their buffer, so the result must not depend on whether a member is
 * a plain property or a getter returning a fresh copy.
 */

function getter(bytes) {
    return {
        enumerable: true,
        get() { return new Uint8Array(bytes); },
    };
}

function getters(alg, members, spray) {
    let obj = Object.assign({}, alg);

    for (let i = 0; i < members.length; i++) {
        Object.defineProperty(obj, members[i], getter(alg[members[i]]));
    }

    if (spray) {
        let value = alg[spray];

        Object.defineProperty(obj, spray, {
            enumerable: true,
            get() {
                let garbage = [];

                for (let i = 0; i < 64; i++) {
                    garbage.push(new Uint8Array(4096).fill(0xaa));
                    garbage.push(new Uint8Array(16).fill(0xaa));
                }

                return value;
            },
        });
    }

    return obj;
}

async function test(params) {
    let key = await crypto.subtle.importKey('raw', params.key_data,
                                            params.alg.name, false,
                                            [params.usage]);

    let call = (alg) => (params.usage == 'encrypt')
                        ? crypto.subtle.encrypt(alg, key, data)
                        : crypto.subtle.deriveBits(alg, key, 256);

    let expected = Buffer.from(await call(params.alg)).toString('hex');
    let r = Buffer.from(await call(getters(params.alg, params.members,
                                           params.spray)))
            .toString('hex');

    if (r !== expected) {
        throw Error(`unexpected output "${r}" != "${expected}"`);
    }

    return 'SUCCESS';
}

let data = new Uint8Array(32);
let aes_key = new Uint8Array(16);
let pw = new Uint8Array([0x70, 0x77]);
let salt = new Uint8Array(4096).fill(1);

let getters_tsuite = {
    name: "algorithm members returned by getters",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: (args) => args,
    tests: [
        { alg: { name: 'AES-GCM', iv: new Uint8Array(12).fill(1),
                 tagLength: 128 },
          key_data: aes_key, usage: 'encrypt', members: ['iv'],
          spray: 'tagLength' },
        { alg: { name: 'AES-GCM', iv: new Uint8Array(12),
                 additionalData: new Uint8Array(16).fill(1) },
          key_data: aes_key, usage: 'encrypt',
          members: ['additionalData'] },
        { alg: { name: 'AES-CTR', counter: new Uint8Array(16).fill(1),
                 length: 64 },
          key_data: aes_key, usage: 'encrypt', members: ['counter'],
          spray: 'length' },
        { alg: { name: 'AES-CBC', iv: new Uint8Array(16).fill(1) },
          key_data: aes_key, usage: 'encrypt', members: ['iv'] },
        { alg: { name: 'PBKDF2', hash: 'SHA-256', salt, iterations: 1 },
          key_data: pw, usage: 'deriveBits', members: ['salt'],
          spray: 'iterations' },
        { alg: { name: 'HKDF', hash: 'SHA-256', salt,
                 info: new Uint8Array(16).fill(2) },
          key_data: pw, usage: 'deriveBits', members: ['salt'],
          spray: 'info' },
        { alg: { name: 'HKDF', hash: 'SHA-256', salt: new Uint8Array(16),
                 info: new Uint8Array(16).fill(2) },
          key_data: pw, usage: 'deriveBits', members: ['info'] },
    ],
};

run([getters_tsuite])
.then($DONE, $DONE);
