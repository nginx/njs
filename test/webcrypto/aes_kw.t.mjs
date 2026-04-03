/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function generate_kek(usages) {
    return await crypto.subtle.generateKey(
        {name: "AES-KW", length: 128}, true, usages);
}


async function test(params) {
    try {
        await crypto.subtle.generateKey(
            {name: "AES-KW", length: 128}, true, ["wrapKey", "unwrapKey"]);
    } catch (e) {
        if (e.message.indexOf("AES-KW") !== -1) {
            return 'SKIPPED';
        }

        throw e;
    }

    let key;

    if (params.generate) {
        key = await crypto.subtle.generateKey(
            {name: "AES-KW", length: params.generate},
            true, ["wrapKey", "unwrapKey"]);

        let raw = await crypto.subtle.exportKey("raw", key);
        if (raw.byteLength !== params.generate / 8) {
            throw Error(`generateKey length mismatch: ${raw.byteLength}`);
        }

        return 'SUCCESS';
    }

    let kek;

    if (params.jwk_import) {
        kek = await crypto.subtle.importKey("jwk", params.jwk_import,
            {name: "AES-KW"}, false, ["wrapKey", "unwrapKey"]);

    } else {
        let extractable = params.jwk_roundtrip ? true : false;

        kek = await crypto.subtle.importKey("raw",
            Buffer.from(params.kek, "hex"),
            {name: "AES-KW"}, extractable, ["wrapKey", "unwrapKey"]);
    }

    if (params.jwk_roundtrip) {
        let jwk = await crypto.subtle.exportKey("jwk", kek);
        if (jwk.kty !== "oct") {
            throw Error(`JWK kty mismatch: ${jwk.kty}`);
        }

        return 'SUCCESS';
    }

    let data = Buffer.from(params.data, "hex");
    let expected = Buffer.from(params.expected, "hex");

    /* import the plaintext as an AES key to wrap */
    let inner = await crypto.subtle.importKey("raw", data,
        {name: "AES-GCM"}, true, ["encrypt"]);

    /* wrap */
    let wrapped = await crypto.subtle.wrapKey("raw", inner, kek,
                                              {name: "AES-KW"});
    wrapped = Buffer.from(wrapped);

    if (wrapped.compare(expected) != 0) {
        throw Error(`AES-KW wrap failed: ${wrapped.toString("hex")}`
                    + ` != ${params.expected}`);
    }

    /* unwrap */
    let unwrapped = await crypto.subtle.unwrapKey("raw", expected, kek,
        {name: "AES-KW"}, {name: "AES-GCM"}, true, ["encrypt"]);

    let raw = await crypto.subtle.exportKey("raw", unwrapped);
    if (Buffer.from(raw).compare(data) != 0) {
        throw Error(`AES-KW unwrap failed`);
    }

    return 'SUCCESS';
}

async function test_wrap_short_data() {
    let kek = await generate_kek(["wrapKey", "unwrapKey"]);
    let key = await crypto.subtle.importKey("raw",
        Buffer.from("0011223344556677", "hex"),
        {name: "HMAC", hash: "SHA-256"}, true, ["sign"]);

    await crypto.subtle.wrapKey("raw", key, kek, {name: "AES-KW"});
}


async function test_wrap_non_multiple() {
    let kek = await generate_kek(["wrapKey", "unwrapKey"]);
    let key = await crypto.subtle.importKey("raw",
        Buffer.from("00112233445566778899AABBCCDDEEFF00112233", "hex"),
        {name: "HMAC", hash: "SHA-256"}, true, ["sign"]);

    await crypto.subtle.wrapKey("raw", key, kek, {name: "AES-KW"});
}


async function test_unwrap_short_data() {
    let kek = await generate_kek(["wrapKey", "unwrapKey"]);

    await crypto.subtle.unwrapKey("raw",
        crypto.getRandomValues(new Uint8Array(23)),
        kek, {name: "AES-KW"}, {name: "AES-GCM"}, true, ["encrypt"]);
}


async function test_unwrap_non_multiple() {
    let kek = await generate_kek(["wrapKey", "unwrapKey"]);

    await crypto.subtle.unwrapKey("raw",
        crypto.getRandomValues(new Uint8Array(25)),
        kek, {name: "AES-KW"}, {name: "AES-GCM"}, true, ["encrypt"]);
}


async function test_no_wrapkey_usage() {
    let kek = await generate_kek(["unwrapKey"]);
    let key = await crypto.subtle.importKey("raw",
        Buffer.from("00112233445566778899AABBCCDDEEFF", "hex"),
        {name: "AES-GCM"}, true, ["encrypt"]);

    await crypto.subtle.wrapKey("raw", key, kek, {name: "AES-KW"});
}


async function test_no_unwrapkey_usage() {
    let kek = await generate_kek(["wrapKey"]);
    let wrapped = Buffer.from(
        "1FA68B0A8112B447AEF34BD8FB5A7B829D3E862371D2CFE5",
        "hex");

    await crypto.subtle.unwrapKey("raw", wrapped, kek,
        {name: "AES-KW"}, {name: "AES-GCM"}, true, ["encrypt"]);
}


let aes_kw_tsuite = {
    name: "AES-KW wrap/unwrap",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: (args) => args,

    tests: [
        /* generateKey */
        { generate: 128 },
        { generate: 192 },
        { generate: 256 },

        /* RFC 3394 4.1: 128-bit KEK, 128-bit data */
        { kek: "000102030405060708090A0B0C0D0E0F",
          data: "00112233445566778899AABBCCDDEEFF",
          expected: "1FA68B0A8112B447AEF34BD8FB5A7B829D3E862371D2CFE5" },

        /* RFC 3394 4.3: 192-bit KEK, 128-bit data */
        { kek: "000102030405060708090A0B0C0D0E0F1011121314151617",
          data: "00112233445566778899AABBCCDDEEFF",
          expected: "96778B25AE6CA435F92B5B97C050AED2468AB8A17AD84E5D" },

        /* RFC 3394 4.5: 256-bit KEK, 128-bit data */
        { kek: "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F",
          data: "00112233445566778899AABBCCDDEEFF",
          expected: "64E8C3F9CE0F5BA263E9777905818A2A93C8191E7D6E8AE7" },

        /* RFC 3394 4.6: 256-bit KEK, 256-bit data */
        { kek: "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F",
          data: "00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F",
          expected: "28C9F404C4B810F4CBCCB35CFB87F8263F5786E2D80ED326CBC7F0E71A99F43BFB988B9B7A02DD21" },

        /* JWK roundtrip: export + reimport */
        { kek: "000102030405060708090A0B0C0D0E0F",
          jwk_roundtrip: true },

        /* JWK import 128-bit */
        { jwk_import: { kty: "oct", k: "AAECAwQFBgcICQoLDA0ODw",
                         alg: "A128KW", key_ops: ["wrapKey", "unwrapKey"] },
          data: "00112233445566778899AABBCCDDEEFF",
          expected: "1FA68B0A8112B447AEF34BD8FB5A7B829D3E862371D2CFE5" },

        /* JWK import 256-bit */
        { jwk_import: { kty: "oct",
                         k: "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8",
                         alg: "A256KW", key_ops: ["wrapKey", "unwrapKey"] },
          data: "00112233445566778899AABBCCDDEEFF",
          expected: "64E8C3F9CE0F5BA263E9777905818A2A93C8191E7D6E8AE7" },
]};

let aes_kw_error_tsuite = {
    name: "AES-KW errors",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: (params) => params.T(),
    prepare_args: (args) => args,

    tests: [
        { T: test_wrap_short_data,
          exception: "TypeError: AES-KW data must be at least 16 bytes" },
        { T: test_wrap_non_multiple,
          exception: "TypeError: AES-KW data must be a multiple of 8 bytes" },
        { T: test_unwrap_short_data,
          exception: "TypeError: AES-KW data must be at least 24 bytes" },
        { T: test_unwrap_non_multiple,
          exception: "TypeError: AES-KW data must be a multiple of 8 bytes" },
        { T: test_no_wrapkey_usage,
          exception: "TypeError: wrapping key does not support wrapKey" },
        { T: test_no_unwrapkey_usage,
          exception: "TypeError: unwrapping key does not support unwrapKey" },
]};

run([aes_kw_tsuite, aes_kw_error_tsuite])
.then($DONE, $DONE);
