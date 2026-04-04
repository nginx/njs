/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    let wrapping_key;

    try {
        wrapping_key = await crypto.subtle.generateKey(
            params.wrap_alg, true, ["wrapKey", "unwrapKey"]);
    } catch (e) {
        if (e.message.indexOf("unknown algorithm") !== -1
            || e.message.indexOf("Unrecognized algorithm") !== -1)
        {
            return 'SKIPPED';
        }

        throw e;
    }

    let key = await crypto.subtle.generateKey(
        params.key_alg, true, params.key_usage);

    let wrapped = await crypto.subtle.wrapKey(
        params.format, key, wrapping_key, params.wrap_params);

    let unwrapped = await crypto.subtle.unwrapKey(
        params.format, wrapped, wrapping_key, params.wrap_params,
        params.key_alg, true, params.key_usage);

    /* verify the unwrapped key works */
    if (params.verify_encrypt) {
        let data = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                   12, 13, 14, 15, 16]);
        let enc = await crypto.subtle.encrypt(
            params.verify_encrypt, unwrapped, data);
        let dec = await crypto.subtle.decrypt(
            params.verify_encrypt, unwrapped, enc);

        if (Buffer.from(dec).compare(Buffer.from(data)) != 0) {
            throw Error("unwrapped key encrypt/decrypt roundtrip failed");
        }
    }

    if (params.verify_sign) {
        let data = new Uint8Array([1, 2, 3, 4]);
        let sig = await crypto.subtle.sign(
            params.verify_sign, unwrapped, data);

        if (sig.byteLength === 0) {
            throw Error("unwrapped key sign produced empty signature");
        }
    }

    /* verify raw key material matches */
    if (params.format === "raw" && params.check_raw) {
        let orig = await crypto.subtle.exportKey("raw", key);
        let unwrapped_raw = await crypto.subtle.exportKey("raw", unwrapped);
        if (Buffer.from(orig).compare(Buffer.from(unwrapped_raw)) != 0) {
            throw Error("unwrapped key raw material mismatch");
        }
    }

    return 'SUCCESS';
}

async function test_non_extractable() {
    let wrapping_key = await crypto.subtle.generateKey(
        {name: "AES-KW", length: 256}, false,
        ["wrapKey", "unwrapKey"]);
    let key = await crypto.subtle.generateKey(
        {name: "AES-GCM", length: 128}, false,
        ["encrypt", "decrypt"]);

    await crypto.subtle.wrapKey("raw", key, wrapping_key,
                                {name: "AES-KW"});
}

async function test_no_wrapKey_usage() {
    let wrapping_key = await crypto.subtle.importKey("raw",
        crypto.getRandomValues(new Uint8Array(16)),
        {name: "AES-KW"}, false, ["unwrapKey"]);
    let key = await crypto.subtle.generateKey(
        {name: "AES-GCM", length: 128}, true,
        ["encrypt", "decrypt"]);

    await crypto.subtle.wrapKey("raw", key, wrapping_key,
                                {name: "AES-KW"});
}

async function test_no_unwrapKey_usage() {
    let wrapping_key = await crypto.subtle.importKey("raw",
        crypto.getRandomValues(new Uint8Array(16)),
        {name: "AES-KW"}, false, ["wrapKey"]);

    let data = crypto.getRandomValues(new Uint8Array(24));
    await crypto.subtle.unwrapKey("raw", data, wrapping_key,
        {name: "AES-KW"}, {name: "AES-GCM"}, true,
        ["encrypt", "decrypt"]);
}

let wrap_tsuite = {
    name: "wrapKey/unwrapKey",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: (args) => args,

    tests: [
        /* AES-KW wrapping an AES-GCM key (raw format) */
        { wrap_alg: {name: "AES-KW", length: 256},
          key_alg: {name: "AES-GCM", length: 128},
          key_usage: ["encrypt", "decrypt"],
          format: "raw",
          wrap_params: {name: "AES-KW"},
          check_raw: true,
          verify_encrypt: {name: "AES-GCM",
                           iv: crypto.getRandomValues(new Uint8Array(12))} },

        /* AES-KW wrapping an AES-CBC key */
        { wrap_alg: {name: "AES-KW", length: 128},
          key_alg: {name: "AES-CBC", length: 256},
          key_usage: ["encrypt", "decrypt"],
          format: "raw",
          wrap_params: {name: "AES-KW"},
          check_raw: true },

        /* AES-KW wrapping an HMAC key */
        { wrap_alg: {name: "AES-KW", length: 256},
          key_alg: {name: "HMAC", hash: "SHA-256"},
          key_usage: ["sign", "verify"],
          format: "raw",
          wrap_params: {name: "AES-KW"},
          check_raw: true,
          verify_sign: {name: "HMAC"} },

        /* AES-GCM wrapping an AES key */
        { wrap_alg: {name: "AES-GCM", length: 256},
          key_alg: {name: "AES-GCM", length: 128},
          key_usage: ["encrypt", "decrypt"],
          format: "raw",
          wrap_params: {name: "AES-GCM",
                        iv: crypto.getRandomValues(new Uint8Array(12))},
          check_raw: true },

        /* AES-CBC wrapping an AES key */
        { wrap_alg: {name: "AES-CBC", length: 256},
          key_alg: {name: "AES-CBC", length: 128},
          key_usage: ["encrypt", "decrypt"],
          format: "raw",
          wrap_params: {name: "AES-CBC",
                        iv: crypto.getRandomValues(new Uint8Array(16))},
          check_raw: true },

        /* AES-GCM wrapping an HMAC key (covers HMAC unwrap path) */
        { wrap_alg: {name: "AES-GCM", length: 256},
          key_alg: {name: "HMAC", hash: "SHA-256"},
          key_usage: ["sign", "verify"],
          format: "raw",
          wrap_params: {name: "AES-GCM",
                        iv: crypto.getRandomValues(new Uint8Array(12))},
          check_raw: true,
          verify_sign: {name: "HMAC"} },

        /* AES-CTR wrapping an AES key */
        { wrap_alg: {name: "AES-CTR", length: 256},
          key_alg: {name: "AES-GCM", length: 128},
          key_usage: ["encrypt", "decrypt"],
          format: "raw",
          wrap_params: {name: "AES-CTR",
                        counter: crypto.getRandomValues(new Uint8Array(16)),
                        length: 64},
          check_raw: true },

        /* AES-GCM wrapping an AES key in JWK format */
        { wrap_alg: {name: "AES-GCM", length: 256},
          key_alg: {name: "AES-GCM", length: 128},
          key_usage: ["encrypt", "decrypt"],
          format: "jwk",
          wrap_params: {name: "AES-GCM",
                        iv: crypto.getRandomValues(new Uint8Array(12))},
          verify_encrypt: {name: "AES-GCM",
                           iv: crypto.getRandomValues(new Uint8Array(12))} },

        /* AES-GCM wrapping an HMAC key in JWK format */
        { wrap_alg: {name: "AES-GCM", length: 256},
          key_alg: {name: "HMAC", hash: "SHA-256"},
          key_usage: ["sign", "verify"],
          format: "jwk",
          wrap_params: {name: "AES-GCM",
                        iv: crypto.getRandomValues(new Uint8Array(12))},
          verify_sign: {name: "HMAC"} },
]};

let wrap_error_tsuite = {
    name: "wrapKey/unwrapKey errors",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: (params) => params.T(),
    prepare_args: (args) => args,

    tests: [
        { T: test_non_extractable, exception: true },
        { T: test_no_wrapKey_usage, exception: true },
        { T: test_no_unwrapKey_usage, exception: true },
]};

run([wrap_tsuite, wrap_error_tsuite])
.then($DONE, $DONE);
