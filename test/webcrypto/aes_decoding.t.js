/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    let enc = base64decode(fs.readFileSync(`test/webcrypto/${params.file}`));
    let key = await crypto.subtle.importKey("raw", params.key,
                                            {name: params.name},
                                            false, ["decrypt"]);

    let plaintext = await crypto.subtle.decrypt(params, key, enc);
    plaintext = new TextDecoder().decode(plaintext);

    if (params.expected != plaintext) {
        throw Error(`${params.name} decoding failed expected: "${params.expected}" vs "${plaintext}"`);
    }

    return 'SUCCESS';
}

function p(args, default_opts) {
    let params = Object.assign({}, default_opts, args);

    params.key = Buffer.from(params.key, "hex");
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
    name: "AES decoding",
    skip: () => (!has_fs() || !has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        key: "00112233001122330011223300112233",
        iv: "44556677445566774455667744556677",
        counter: "44556677445566774455667744556677",
        length: 64
    },

    tests: [
        { name: "AES-GCM", file: "text.base64.aes-gcm128.enc",
          expected: "AES-GCM-SECRET-TEXT" },
        { name: "AES-GCM", file: "text.base64.aes-gcm128-96.enc",
          exception: "Error: EVP_DecryptFinal_ex() failed" },
        { name: "AES-GCM", file: "text.base64.aes-gcm128-96.enc", tagLength: 96,
          expected: "AES-GCM-96-TAG-LENGTH-SECRET-TEXT" },
        { name: "AES-GCM", file: "text.base64.aes-gcm128-extra.enc", additionalData: "deadbeef",
          expected: "AES-GCM-ADDITIONAL-DATA-SECRET-TEXT" },
        { name: "AES-GCM", file: "text.base64.aes-gcm256.enc",
          key: "0011223300112233001122330011223300112233001122330011223300112233",
          expected: "AES-GCM-256-SECRET-TEXT" },
        { name: "AES-GCM", file: "text.base64.aes-gcm256.enc",
          key: "00112233001122330011223300112233001122330011223300112233001122",
          exception: "TypeError: AES-GCM Invalid key length" },
        { name: "AES-CTR", file: "text.base64.aes-ctr128.enc",
          expected: "AES-CTR-SECRET-TEXT" },
        { name: "AES-CTR", file: "text.base64.aes-ctr256.enc",
          key: "0011223300112233001122330011223300112233001122330011223300112233",
          expected: "AES-CTR-256-SECRET-TEXT" },
        { name: "AES-CBC", file: "text.base64.aes-cbc128.enc",
          expected: "AES-CBC-SECRET-TEXT" },
        { name: "AES-CBC", file: "text.base64.aes-cbc256.enc",
          key: "0011223300112233001122330011223300112233001122330011223300112233",
          expected: "AES-CBC-256-SECRET-TEXT" },
]};

run([aes_tsuite])
.then($DONE, $DONE);
