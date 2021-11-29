/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    let spki = await crypto.subtle.importKey("spki",
                            pem_to_der(fs.readFileSync(`test/webcrypto/${params.spki}`), "PUBLIC"),
                            {name:"RSA-OAEP", hash:params.spki_hash},
                            false, ["encrypt"]);

    let pkcs8 = await crypto.subtle.importKey("pkcs8",
                            pem_to_der(fs.readFileSync(`test/webcrypto/${params.pkcs8}`), "PRIVATE"),
                            {name:"RSA-OAEP", hash:params.pkcs8_hash},
                            false, ["decrypt"]);

    let enc = await crypto.subtle.encrypt({name: "RSA-OAEP"}, spki, params.data);

    let plaintext = await crypto.subtle.decrypt({name: "RSA-OAEP"}, pkcs8, enc);

    plaintext = Buffer.from(plaintext);

    if (params.data.compare(plaintext) != 0) {
        throw Error(`RSA-OAEP encoding/decoding failed expected: "${params.data}" vs "${plaintext}"`);
    }

    return 'SUCCESS';
}

function p(args, default_opts) {
    let params = Object.assign({}, default_opts, args);

    params.data = Buffer.from(params.data, "hex");

    return params;
}

let rsa_tsuite = {
    name: "RSA-OAEP encoding/decoding",
    skip: () => (!has_fs() || !has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        spki: "rsa.spki",
        spki_hash: "SHA-256",
        pkcs8: "rsa.pkcs8",
        pkcs8_hash: "SHA-256",
    },

    tests: [
        { data: "aabbcc" },
        { data: "aabbccdd".repeat(4) },
        { data: "aabbccdd".repeat(7) },
        { data: "aabbcc", spki_hash: "SHA-1", pkcs8_hash: "SHA-1" },
        { data: "aabbccdd".repeat(4), spki_hash: "SHA-1", pkcs8_hash: "SHA-1" },
        { data: "aabbccdd".repeat(7), spki_hash: "SHA-1", pkcs8_hash: "SHA-1" },
        { data: "aabbcc", spki_hash: "SHA-384", pkcs8_hash: "SHA-384" },
        { data: "aabbccdd".repeat(4), spki_hash: "SHA-384", pkcs8_hash: "SHA-384" },
        { data: "aabbccdd".repeat(7), spki_hash: "SHA-384", pkcs8_hash: "SHA-384" },

        { data: "aabbcc", spki_hash: "SHA-256", pkcs8_hash: "SHA-384", exception: "Error: EVP_PKEY_decrypt() failed" },
        { data: "aabbcc", spki_hash: "XXX", exception: "TypeError: unknown hash name: \"XXX\"" },
        { data: "aabbcc", spki: "rsa.spki.broken", exception: "Error: d2i_PUBKEY() failed" },
        { data: "aabbcc", spki: "rsa2.spki", exception: "Error: EVP_PKEY_decrypt() failed" },
]};

run([rsa_tsuite])
.then($DONE, $DONE);
