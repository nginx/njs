/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    try {
        await crypto.subtle.generateKey("Ed25519", true, ["sign", "verify"]);
    } catch (e) {
        if (e.message.indexOf("Ed25519") !== -1) {
            return 'SKIPPED';
        }

        throw e;
    }

    /* generateKey + sign/verify roundtrip */
    if (params.generate) {
        let kp = await crypto.subtle.generateKey("Ed25519", true,
                                                  ["sign", "verify"]);

        let data = new TextEncoder().encode(params.text || "test data");
        let sig = await crypto.subtle.sign("Ed25519", kp.privateKey, data);

        if (sig.byteLength !== 64) {
            throw Error(`signature length ${sig.byteLength}, expected 64`);
        }

        let ok = await crypto.subtle.verify("Ed25519", kp.publicKey, sig,
                                            data);
        if (!ok) {
            throw Error("verify failed for valid signature");
        }

        return 'SUCCESS';
    }

    /* raw export/import roundtrip */
    if (params.raw_roundtrip) {
        let kp = await crypto.subtle.generateKey("Ed25519", true,
                                                  ["sign", "verify"]);

        let data = new TextEncoder().encode("raw test");
        let sig = await crypto.subtle.sign("Ed25519", kp.privateKey, data);

        let raw = await crypto.subtle.exportKey("raw", kp.publicKey);
        let imp = await crypto.subtle.importKey("raw", raw,
            "Ed25519", true, ["verify"]);

        let ok = await crypto.subtle.verify("Ed25519", imp, sig, data);
        if (!ok) {
            throw Error("verify failed with re-imported raw key");
        }

        return 'SUCCESS';
    }

    /* JWK export/import roundtrip */
    if (params.jwk_roundtrip) {
        let kp = await crypto.subtle.generateKey("Ed25519", true,
                                                  ["sign", "verify"]);

        let jwk = await crypto.subtle.exportKey("jwk", kp.privateKey);
        if (jwk.kty !== "OKP" || jwk.crv !== "Ed25519" || !("d" in jwk)) {
            throw Error(`bad JWK: ${JSON.stringify(jwk)}`);
        }

        let imp = await crypto.subtle.importKey("jwk", jwk,
            "Ed25519", true, ["sign"]);

        let data = new TextEncoder().encode("jwk test");
        let sig = await crypto.subtle.sign("Ed25519", imp, data);
        let ok = await crypto.subtle.verify("Ed25519", kp.publicKey,
                                            sig, data);
        if (!ok) {
            throw Error("verify failed with re-imported JWK key");
        }

        return 'SUCCESS';
    }

    /* PKCS8/SPKI roundtrip */
    if (params.pkcs8_roundtrip) {
        let kp = await crypto.subtle.generateKey("Ed25519", true,
                                                  ["sign", "verify"]);

        let pkcs8 = await crypto.subtle.exportKey("pkcs8", kp.privateKey);
        let spki = await crypto.subtle.exportKey("spki", kp.publicKey);

        let priv = await crypto.subtle.importKey("pkcs8", pkcs8,
            "Ed25519", true, ["sign"]);
        let pub = await crypto.subtle.importKey("spki", spki,
            "Ed25519", true, ["verify"]);

        let data = new TextEncoder().encode("pkcs8 test");
        let sig = await crypto.subtle.sign("Ed25519", priv, data);
        let ok = await crypto.subtle.verify("Ed25519", pub, sig, data);
        if (!ok) {
            throw Error("verify failed with PKCS8/SPKI keys");
        }

        return 'SUCCESS';
    }

    /* verify with tampered data must return false */
    if (params.verify_tampered_data) {
        let kp = await crypto.subtle.generateKey("Ed25519", true,
                                                  ["sign", "verify"]);

        let data = new TextEncoder().encode("original");
        let sig = await crypto.subtle.sign("Ed25519", kp.privateKey, data);

        let tampered = new TextEncoder().encode("tampered");
        let ok = await crypto.subtle.verify("Ed25519", kp.publicKey, sig,
                                            tampered);
        if (ok) {
            throw Error("verify must fail for tampered data");
        }

        return 'SUCCESS';
    }

    /* verify with wrong key must return false */
    if (params.verify_wrong_key) {
        let kp1 = await crypto.subtle.generateKey("Ed25519", true,
                                                    ["sign", "verify"]);
        let kp2 = await crypto.subtle.generateKey("Ed25519", true,
                                                    ["sign", "verify"]);

        let data = new TextEncoder().encode("test");
        let sig = await crypto.subtle.sign("Ed25519", kp1.privateKey, data);

        let ok = await crypto.subtle.verify("Ed25519", kp2.publicKey, sig,
                                            data);
        if (ok) {
            throw Error("verify must fail with wrong key");
        }

        return 'SUCCESS';
    }

    /* RFC 8032 test vector: sign-only (verify uses derived public key) */
    if (params.rfc8032) {
        let priv_jwk = {
            kty: "OKP",
            crv: "Ed25519",
            d: params.d,
            x: params.x,
        };

        let priv = await crypto.subtle.importKey("jwk", priv_jwk,
            "Ed25519", false, ["sign"]);

        let data = new Uint8Array(params.msg || []);
        let sig = await crypto.subtle.sign("Ed25519", priv, data);

        let expected = Buffer.from(params.expected, "hex");
        if (Buffer.from(sig).compare(expected) !== 0) {
            throw Error("RFC 8032 signature mismatch:\n"
                + Buffer.from(sig).toString("hex") + "\n"
                + "expected:\n" + params.expected);
        }

        return 'SUCCESS';
    }

    return 'SUCCESS';
}

let ed25519_tsuite = {
    name: "Ed25519 sign/verify",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: (args) => args,

    tests: [
        { generate: true },
        { raw_roundtrip: true },
        { jwk_roundtrip: true },
        { pkcs8_roundtrip: true },

        /* RFC 8032 Test 1: empty message */
        { rfc8032: true,
          d: "nWGxne_9WmC6hEr0kuwsxERJxWl7MmkZcDusAxyuf2A",
          x: "11qYAYKxCrfVS_7TyWQHOg7hcvPapiMlrwIaaPcHURo",
          msg: [],
          expected: "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },

        /* RFC 8032 Test 2: 1-byte message 0x72 */
        { rfc8032: true,
          d: "TM0Imyj_ltqdtsNG7BFOD1uKMZ81q6Yk2oz27U-4pvs",
          x: "PUAXw-hDiVqStwqnTRt-vJyYLM8uxJaMwM1V8Sr0Zgw",
          msg: [0x72],
          expected: "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },

        { verify_tampered_data: true },
        { verify_wrong_key: true },
]};

async function test_sign_with_x25519() {
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

async function test_derive_with_ed25519() {
    try {
        await crypto.subtle.generateKey("Ed25519", true,
                                         ["sign", "verify"]);
    } catch (e) {
        if (e.message.indexOf("Ed25519") !== -1) {
            return 'SKIPPED';
        }

        throw e;
    }

    let kp = await crypto.subtle.generateKey("Ed25519", true,
                                              ["sign", "verify"]);
    await crypto.subtle.deriveBits(
        {name: "X25519", public: kp.publicKey},
        kp.privateKey, 256);
}

let ed25519_error_tsuite = {
    name: "Ed25519 errors",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: (params) => params.T(),
    prepare_args: (args) => args,

    tests: [
        { T: test_sign_with_x25519, exception: true },
        { T: test_derive_with_ed25519, exception: true },
]};

run([ed25519_tsuite, ed25519_error_tsuite])
.then($DONE, $DONE);
