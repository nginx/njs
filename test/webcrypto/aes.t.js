/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    let dkey = await crypto.subtle.importKey("raw", params.key,
                                       {name: params.name},
                                       false, ["decrypt"]);

    let ekey = await crypto.subtle.importKey("raw", params.key,
                                       {name: params.name},
                                       false, ["encrypt"]);

    let enc = await crypto.subtle.encrypt(params, ekey, params.data);
    let plaintext = await crypto.subtle.decrypt(params, dkey, enc);
    plaintext = Buffer.from(plaintext);

    if (params.data.compare(plaintext) != 0) {
        throw Error(`${params.name} encoding/decoding failed length ${data.length}`);
    }

    return 'SUCCESS';
}

function p(args, default_opts) {
    let params = Object.assign({}, default_opts, args);

    params.key = Buffer.from(params.key, "hex");
    params.data = Buffer.from(params.data, "hex");
    params.iv = Buffer.from(params.iv, "hex");
    params.counter = Buffer.from(params.counter, "hex");

    switch (params.name) {
    case "AES-GCM":
        if (params.additionalData) {
            params.additionalData = Buffer.from(params.additionalData, "hex");
        }

        break;
    }

    return params;
}


let aes_tsuite = {
    name: "AES encoding/decoding",
    skip: () => (!has_fs() || !has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        iv: "44556677445566774455667744556677",
        key: "00112233001122330011223300112233",
        counter: "44556677445566774455667744556677",
        length: 64
    },

    tests: [
        { name: "AES-gcm", data: "aa" },
        { name: "aes-gcm", data: "aabbcc" },
        { name: "AES-GCM", data: "aabbcc", additionalData: "deafbeef"},
        { name: "AES-GCM", data: "aabbccdd".repeat(4) },
        { name: "AES-GCM", data: "aa", iv: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" },
        { name: "AES-GCM", data: "aabbcc", tagLength: 96 },
        { name: "AES-GCM", data: "aabbcc", tagLength: 112 },
        { name: "AES-GCM", data: "aabbcc", tagLength: 113, exception: "TypeError: AES-GCM Invalid tagLength" },
        { name: "AES-GCM", data: "aabbccdd".repeat(4096) },

        { name: "AES-CTR", data: "aa" },
        { name: "AES-CTR", data: "aabbcc" },
        { name: "AES-CTR", data: "aabbccdd".repeat(4) },
        { name: "AES-CTR", data: "aabbccdd".repeat(4096) },
        { name: "AES-CTR", data: "aa", counter: "ffffffffffffffffffffffffffffffff" },
        { name: "AES-CTR", data: "aa", counter: "ffffffff",
          exception: "TypeError: AES-CTR algorithm.counter must be 16 bytes long" },
        { name: "AES-CTR", data: "aabbcc", counter: "ffffffffffffffffffffffffffffffff" },
        { name: "AES-CTR", data: "aabbccdd".repeat(5), counter: "ffffffffffffffffffffffffffffffff" },
        { name: "AES-CTR", data: "aabbccdd".repeat(4096), counter: "fffffffffffffffffffffffffffffff0" },
        { name: "AES-CTR", data: "aabbccdd".repeat(4096), counter: "ffffffffffffffffffffffffffffffff" },
        { name: "AES-CTR", data: "aabbccdd".repeat(4096), counter: "ffffffffffffffffffffffffffffffff", length: 7,
          exception: "TypeError: AES-CTR repeated counter" },
        { name: "AES-CTR", data: "aabbccdd".repeat(4096), counter: "ffffffffffffffffffffffffffffffff", length: 11 },
        { name: "AES-CTR", data: "aabbccdd".repeat(4096), length: 20 },
        { name: "AES-CTR", data: "aabbccdd".repeat(4096), length: 24 },
        { name: "AES-CTR", data: "aabbccdd", length: 129,
          exception: "TypeError: AES-CTR algorithm.length must be between 1 and 128" },

        { name: "AES-CBC", data: "aa" },
        { name: "AES-CBC", data: "aabbccdd".repeat(4) },
        { name: "AES-CBC", data: "aabbccdd".repeat(4096) },
        { name: "AES-CBC", data: "aabbccdd".repeat(5), iv: "ffffffffffffffffffffffffffffffff" },
]};

run([aes_tsuite])
.then($DONE, $DONE);
