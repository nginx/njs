/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function import_key_pair(params) {
    let pair = {};

    pair.privateKey = await crypto.subtle.importKey("pkcs8",
        Buffer.from(params.private_pkcs8, "base64url"),
        "X25519", true, ["deriveBits", "deriveKey"]);

    pair.publicKey = await crypto.subtle.importKey("spki",
        Buffer.from(params.public_spki, "base64url"),
        "X25519", true, []);

    return pair;
}


async function test(params) {
    try {
        await crypto.subtle.generateKey("X25519", true, ["deriveBits"]);
    } catch (e) {
        if (e.message.indexOf("X25519") !== -1) {
            return 'SKIPPED';
        }

        throw e;
    }

    /* generateKey + deriveBits roundtrip */
    if (params.derive_bits) {
        let alice = await crypto.subtle.generateKey("X25519", true,
            ["deriveBits", "deriveKey"]);
        let bob = await crypto.subtle.generateKey("X25519", true,
            ["deriveBits", "deriveKey"]);

        let s1 = await crypto.subtle.deriveBits(
            {name: "X25519", public: bob.publicKey},
            alice.privateKey, 256);
        let s2 = await crypto.subtle.deriveBits(
            {name: "X25519", public: alice.publicKey},
            bob.privateKey, 256);

        if (Buffer.from(s1).compare(Buffer.from(s2)) !== 0) {
            throw Error("shared secrets do not match");
        }

        if (s1.byteLength !== 32) {
            throw Error(`shared secret length: ${s1.byteLength}`);
        }

        return 'SUCCESS';
    }

    /* deriveKey to AES-GCM */
    if (params.derive_key) {
        let alice = await crypto.subtle.generateKey("X25519", true,
            ["deriveBits", "deriveKey"]);
        let bob = await crypto.subtle.generateKey("X25519", true,
            ["deriveBits", "deriveKey"]);

        let aes = await crypto.subtle.deriveKey(
            {name: "X25519", public: bob.publicKey},
            alice.privateKey,
            {name: "AES-GCM", length: 256},
            true, ["encrypt", "decrypt"]);

        let iv = crypto.getRandomValues(new Uint8Array(12));
        let data = new TextEncoder().encode("test data");

        let enc = await crypto.subtle.encrypt(
            {name: "AES-GCM", iv: iv}, aes, data);
        let dec = await crypto.subtle.decrypt(
            {name: "AES-GCM", iv: iv}, aes, enc);

        if (Buffer.from(dec).compare(Buffer.from(data)) !== 0) {
            throw Error("deriveKey encrypt/decrypt failed");
        }

        return 'SUCCESS';
    }

    /* RFC 7748 vector: imported keys must derive the expected secret */
    if (params.derive_bits_vector) {
        let alice = await import_key_pair(params.alice);
        let bob = await import_key_pair(params.bob);

        let s1 = await crypto.subtle.deriveBits(
            {name: "X25519", public: bob.publicKey},
            alice.privateKey, 256);
        let s2 = await crypto.subtle.deriveBits(
            {name: "X25519", public: alice.publicKey},
            bob.privateKey, 256);

        s1 = Buffer.from(s1).toString("base64url");
        s2 = Buffer.from(s2).toString("base64url");

        if (s1 !== params.expected) {
            throw Error(`shared secret mismatch: ${s1} != ${params.expected}`);
        }

        if (s2 !== params.expected) {
            throw Error(`reverse shared secret mismatch: ${s2} != `
                        + `${params.expected}`);
        }

        return 'SUCCESS';
    }

    /* raw export/import roundtrip */
    if (params.raw_roundtrip) {
        let kp = await crypto.subtle.generateKey("X25519", true,
            ["deriveBits", "deriveKey"]);

        let raw = await crypto.subtle.exportKey("raw", kp.publicKey);
        if (raw.byteLength !== 32) {
            throw Error(`raw pub length: ${raw.byteLength}`);
        }

        let imported = await crypto.subtle.importKey("raw", raw,
            "X25519", true, []);

        let raw2 = await crypto.subtle.exportKey("raw", imported);
        if (Buffer.from(raw).compare(Buffer.from(raw2)) !== 0) {
            throw Error("raw roundtrip mismatch");
        }

        return 'SUCCESS';
    }

    /* JWK export/import roundtrip */
    if (params.jwk_roundtrip) {
        let kp = await crypto.subtle.generateKey("X25519", true,
            ["deriveBits", "deriveKey"]);

        let jwk = await crypto.subtle.exportKey("jwk", kp.privateKey);
        if (jwk.kty !== "OKP" || jwk.crv !== "X25519") {
            throw Error(`bad JWK: kty=${jwk.kty} crv=${jwk.crv}`);
        }

        if (!("d" in jwk)) {
            throw Error("private JWK missing d");
        }

        let imported = await crypto.subtle.importKey("jwk", jwk,
            "X25519", true, ["deriveBits", "deriveKey"]);

        let jwk2 = await crypto.subtle.exportKey("jwk", imported);
        if (jwk.d !== jwk2.d || jwk.x !== jwk2.x) {
            throw Error("JWK roundtrip mismatch");
        }

        /* public JWK */
        let pub_jwk = await crypto.subtle.exportKey("jwk", kp.publicKey);
        if ("d" in pub_jwk) {
            throw Error("public JWK should not have d");
        }

        return 'SUCCESS';
    }

    /* PKCS8/SPKI roundtrip */
    if (params.pkcs8_roundtrip) {
        let kp = await crypto.subtle.generateKey("X25519", true,
            ["deriveBits", "deriveKey"]);

        let pkcs8 = await crypto.subtle.exportKey("pkcs8", kp.privateKey);
        let spki = await crypto.subtle.exportKey("spki", kp.publicKey);

        let priv = await crypto.subtle.importKey("pkcs8", pkcs8,
            "X25519", true, ["deriveBits"]);
        let pub = await crypto.subtle.importKey("spki", spki,
            "X25519", true, []);

        let s = await crypto.subtle.deriveBits(
            {name: "X25519", public: pub}, priv, 256);
        if (s.byteLength !== 32) {
            throw Error("PKCS8/SPKI derive failed");
        }

        return 'SUCCESS';
    }

    return 'SUCCESS';
}

let x25519_tsuite = {
    name: "X25519 key agreement",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: (args) => args,

    tests: [
        { derive_bits: true },
        { derive_key: true },
        { derive_bits_vector: true,
          alice: {
              private_pkcs8: "MC4CAQAwBQYDK2VuBCIEIHcHbQpzGKV9PBbBclGyZkXfTC-H68CZKrF3-6UduSwq",
              public_spki: "MCowBQYDK2VuAyEAhSDwCYkwp1R0i33ctD73Wg2_Og0mOBr066SpjqqbTmo",
          },
          bob: {
              private_pkcs8: "MC4CAQAwBQYDK2VuBCIEIF2rCH5iSopLeeF_i4OADuZvO7EpJhi2_Rwviyf_iODr",
              public_spki: "MCowBQYDK2VuAyEA3p7bfXt9wbTTW2HC7OQ1Nz-DQ8hbeGdNrfx-FG-IK08",
          },
          expected: "Sl2dW6TOLeFyjjv0gDUPJeB-IclH0Z4zdvCbPB4WF0I" },
        { raw_roundtrip: true },
        { jwk_roundtrip: true },
        { pkcs8_roundtrip: true },
]};

async function test_derive_with_ed25519_key() {
    try {
        await crypto.subtle.generateKey("Ed25519", true, ["sign"]);
    } catch (e) {
        if (e.message.indexOf("Ed25519") !== -1) {
            return 'SKIPPED';
        }

        throw e;
    }

    let x_kp = await crypto.subtle.generateKey("X25519", true,
                                                ["deriveBits"]);
    let ed_kp = await crypto.subtle.generateKey("Ed25519", true,
                                                 ["sign"]);

    await crypto.subtle.deriveBits(
        {name: "X25519", public: ed_kp.publicKey},
        x_kp.privateKey, 256);
}

async function test_sign_with_x25519_key() {
    try {
        await crypto.subtle.generateKey("X25519", true, ["deriveBits"]);
    } catch (e) {
        if (e.message.indexOf("X25519") !== -1) {
            return 'SKIPPED';
        }

        throw e;
    }

    let kp = await crypto.subtle.generateKey("X25519", true,
                                              ["deriveBits"]);
    let data = new TextEncoder().encode("test");
    await crypto.subtle.sign("Ed25519", kp.privateKey, data);
}

let x25519_error_tsuite = {
    name: "X25519 errors",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: (params) => params.T(),
    prepare_args: (args) => args,

    tests: [
        { T: test_derive_with_ed25519_key, exception: true },
        { T: test_sign_with_x25519_key, exception: true },
]};

run([x25519_tsuite, x25519_error_tsuite])
.then($DONE, $DONE);
