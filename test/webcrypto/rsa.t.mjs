/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function encrypt_key(params) {
    if (params.generate_keys) {
        if (params.generate_keys.publicKey) {
            return params.generate_keys.publicKey;
        }

        params.generate_keys = await crypto.subtle.generateKey(params.generate_keys.alg,
                                                               params.generate_keys.extractable,
                                                               params.generate_keys.usage);

        return params.generate_keys.publicKey;
    }

    return await crypto.subtle.importKey(params.enc.fmt,
                                         params.enc.key,
                                         { name: "RSA-OAEP", hash:params.enc.hash },
                                         false, ["encrypt"]);

}

async function decrypt_key(params) {
    if (params.generate_keys) {
        if (params.generate_keys.privateKey) {
            return params.generate_keys.privateKey;
        }

        params.generate_keys = await crypto.subtle.generateKey(params.generate_keys.alg,
                                                               params.generate_keys.extractable,
                                                               params.generate_keys.usage);

        return params.generate_keys.privateKey;
    }

    return await crypto.subtle.importKey(params.dec.fmt,
                                         params.dec.key,
                                         { name: "RSA-OAEP", hash:params.dec.hash },
                                         false, ["decrypt"]);
}

async function test(params) {
    let enc_key = await encrypt_key(params);
    let dec_key = await decrypt_key(params);

    let enc = await crypto.subtle.encrypt({name: "RSA-OAEP"}, enc_key, params.data);

    let plaintext = await crypto.subtle.decrypt({name: "RSA-OAEP"}, dec_key, enc);

    plaintext = Buffer.from(plaintext);

    if (params.data.compare(plaintext) != 0) {
        throw Error(`RSA-OAEP encoding/decoding failed expected: "${params.data}" vs "${plaintext}"`);
    }

    return 'SUCCESS';
}

function p(args, default_opts) {
    let key;
    let params = merge({}, default_opts);
    params = merge(params, args);

    switch (params.enc.fmt) {
    case "spki":
        let pem = fs.readFileSync(`test/webcrypto/${params.enc.key}`);
        key = pem_to_der(pem, "PUBLIC");
        break;
    case "jwk":
        key = load_jwk(params.enc.key);
        break;
    default:
        throw Error("Unknown encoding key format");
    }

    params.enc.key = key;

    switch (params.dec.fmt) {
    case "pkcs8":
        let pem = fs.readFileSync(`test/webcrypto/${params.dec.key}`);
        key = pem_to_der(pem, "PRIVATE");
        break;
    case "jwk":
        key = load_jwk(params.dec.key);
        break;
    default:
        throw Error("Unknown decoding key format");
    }

    params.dec.key = key;

    params.data = Buffer.from(params.data, "hex");

    return params;
}

let rsa_tsuite = {
    name: "RSA-OAEP encoding/decoding",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        enc: { fmt: "spki", key: "rsa.spki", hash: "SHA-256" },
        dec: { fmt: "pkcs8", key: "rsa.pkcs8", hash: "SHA-256" },
    },

    tests: [
        { data: "aabbcc" },
        { data: "aabbccdd".repeat(4) },
        { data: "aabbccdd".repeat(7) },
        { data: "aabbcc",
          generate_keys: { alg: { name: "RSA-OAEP",
                                  modulusLength: 2048,
                                  publicExponent: new Uint8Array([1, 0, 1]),
                                  hash: "SHA-256" },
                           extractable: true,
                           usage: [ "encrypt", "decrypt" ] },
          expected: true },
        { data: "aabbcc", enc: { hash: "SHA-1" }, dec: { hash: "SHA-1" } },
        { data: "aabbccdd".repeat(4), enc: { hash: "SHA-1" }, dec: { hash: "SHA-1" } },
        { data: "aabbccdd".repeat(7), enc: { hash: "SHA-1" }, dec: { hash: "SHA-1" } },
        { data: "aabbcc", enc: { hash: "SHA-384" }, dec: { hash: "SHA-384" } },
        { data: "aabbccdd".repeat(4), enc: { hash: "SHA-384" }, dec: { hash: "SHA-384" } },
        { data: "aabbccdd".repeat(7), enc: { hash: "SHA-384" }, dec: { hash: "SHA-384" } },

        { data: "aabbcc", enc: { hash: "SHA-256" }, dec: { hash: "SHA-384" }, exception: "Error: EVP_PKEY_decrypt() failed" },
        { data: "aabbcc", enc: { hash: "XXX" }, exception: "TypeError: unknown hash name: \"XXX\"" },
        { data: "aabbcc", dec: { key: "rsa.spki.broken" }, exception: "Error: d2i_PUBKEY() failed" },
        { data: "aabbcc", dec: { key: "rsa2.spki" }, exception: "Error: EVP_PKEY_decrypt() failed" },

        { data: "aabbcc", enc: { fmt: "jwk", key: "rsa.enc.pub.jwk" }, dec: { fmt: "jwk", key: "rsa.dec.jwk" } },
]};

run([rsa_tsuite])
.then($DONE, $DONE);
