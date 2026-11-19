/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, webCryptoUtils.js, runTsuite.js]
flags: [async]
---*/

async function testDeriveBits(params) {
    let aliceKeyPair = await load_key_pair(params.pair[0]);
    let bobKeyPair = await load_key_pair(params.pair[1]);

    let ecdhParams = { name: "ECDH", public: bobKeyPair.publicKey };

    let result = await crypto.subtle.deriveBits(ecdhParams, aliceKeyPair.privateKey, params.length);
    result = Buffer.from(result).toString('base64url');

    if (result !== params.expected) {
        throw Error(`ECDH deriveBits failed expected: "${params.expected}" vs "${result}"`);
    }

    let ecdhParamsReverse = { name: "ECDH", public: aliceKeyPair.publicKey };

    let secondResult = await crypto.subtle.deriveBits(ecdhParamsReverse, bobKeyPair.privateKey, params.length);
    secondResult = Buffer.from(secondResult).toString('base64url');

    if (secondResult !== params.expected) {
        throw Error(`ECDH reverse deriveBits failed expected: "${params.expected}" vs "${secondResult}"`);
    }

    return "SUCCESS";
}

function deriveCurveFromName(name) {
    if (/secp384r1/.test(name)) {
        return "P-384";
    }

    if (/secp521r1/.test(name)) {
        return "P-521";
    }

    return "P-256";
}

async function load_key_pair(name) {
    let pair = {};
    let pem = fs.readFileSync(`test/webcrypto/${name}.pkcs8`);
    let key = pem_to_der(pem, "private");

    pair.privateKey = await crypto.subtle.importKey("pkcs8", key,
                                                    { name: "ECDH", namedCurve: deriveCurveFromName(name) },
                                                    true, ["deriveBits", "deriveKey"]);

    pem = fs.readFileSync(`test/webcrypto/${name}.spki`);
    key = pem_to_der(pem, "public");
    pair.publicKey = await crypto.subtle.importKey("spki", key,
                                                   { name: "ECDH", namedCurve: deriveCurveFromName(name) },
                                                   true, []);

    return pair;
}

let ecdh_bits_tsuite = {
    name: "ECDH-DeriveBits",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: testDeriveBits,
    opts: {
        pair: ['ec', 'ec2'],
        length: 256
    },
    tests: [
        { expected: "mMAGhQ_1Wr3u6Y6VyzVuolCA7x8RM-15e73laLJMUok" },
        { pair: ['ec_secp384r1', 'ec2_secp384r1'],
          expected: "4OmeRzZZ53eCgn09zI2TumH4n4Zp-nfHsfZTOBEu8Hg" },
        { pair: ['ec_secp384r1', 'ec2_secp384r1'],
          length: 384,
          expected: "4OmeRzZZ53eCgn09zI2TumH4n4Zp-nfHsfZTOBEu8HjB0GF2YrOw5dCUgavKZaNR" },
        { pair: ['ec_secp521r1', 'ec2_secp521r1'],
          length: 528,
          expected: "ATBls20ukLQI7AJQ6LRnyD6wLDR_FDmBoAdVX5_DB_bMDe_uYMjN-jQqPTkGNIo6NOqmXMX9KNQ-AqL8aPjySMMm" },
    ]
};

async function testDeriveKey(params) {
    let aliceKeyPair = await load_key_pair(params.pair[0]);
    let bobKeyPair = await load_key_pair(params.pair[1]);
    let eveKeyPair = await crypto.subtle.generateKey({ name: "ECDH", namedCurve: deriveCurveFromName(params.pair[0]) },
                                                     true, ["deriveKey", "deriveBits"]);

    let ecdhParamsAlice = { name: "ECDH", public: bobKeyPair.publicKey };
    let ecdhParamsBob = { name: "ECDH", public: aliceKeyPair.publicKey };
    let ecdhParamsEve = { name: "ECDH", public: eveKeyPair.publicKey };

    let derivedAlgorithm = { name: params.derivedAlgorithm.name };

    derivedAlgorithm.length = params.derivedAlgorithm.length;
    derivedAlgorithm.hash = params.derivedAlgorithm.hash;

    let aliceDerivedKey = await crypto.subtle.deriveKey(ecdhParamsAlice, aliceKeyPair.privateKey,
                                                        derivedAlgorithm, params.extractable, params.usage);

    if (aliceDerivedKey.extractable !== params.extractable) {
        throw Error(`ECDH extractable test failed: ${params.extractable} vs ${aliceDerivedKey.extractable}`);
    }

    if (compareUsage(aliceDerivedKey.usages, params.usage) !== true) {
        throw Error(`ECDH usage test failed: ${params.usage} vs ${aliceDerivedKey.usages}`);
    }

    let bobDerivedKey = await crypto.subtle.deriveKey(ecdhParamsBob, bobKeyPair.privateKey,
                                                      derivedAlgorithm, params.extractable, params.usage);

    let eveDerivedKey = await crypto.subtle.deriveKey(ecdhParamsEve, eveKeyPair.privateKey,
                                                      derivedAlgorithm, params.extractable, params.usage);

    if (params.extractable &&
        (params.derivedAlgorithm.name === "AES-GCM"
         || params.derivedAlgorithm.name === "AES-CBC"
         || params.derivedAlgorithm.name === "AES-CTR"
         || params.derivedAlgorithm.name === "HMAC"))
    {
        const aliceRawKey = await crypto.subtle.exportKey("raw", aliceDerivedKey);
        const bobRawKey = await crypto.subtle.exportKey("raw", bobDerivedKey);
        const eveRawKey = await crypto.subtle.exportKey("raw", eveDerivedKey);

        const aliceKeyData = Buffer.from(aliceRawKey).toString("base64url");
        const bobKeyData = Buffer.from(bobRawKey).toString("base64url");
        const eveKeyData = Buffer.from(eveRawKey).toString("base64url");

        if (aliceKeyData !== bobKeyData) {
            throw Error(`ECDH key symmetry test failed: keys are not equal`);
        }

        if (aliceKeyData !== params.expected) {
            throw Error(`ECDH key symmetry test failed: expected: "${params.expected}" vs "${aliceKeyData}"`);
        }

        if (aliceKeyData === eveKeyData) {
            throw Error(`ECDH key symmetry test failed: keys are equal`);
        }
    }

    return "SUCCESS";
}

let ecdh_key_tsuite = {
    name: "ECDH-DeriveKey",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: testDeriveKey,
    opts: {
        pair: ['ec', 'ec2'],
        extractable: true,
        derivedAlgorithm: {
            name: "AES-GCM",
            length: 256
        },
        expected: "mMAGhQ_1Wr3u6Y6VyzVuolCA7x8RM-15e73laLJMUok",
        usage: ["encrypt", "decrypt"]
    },
    tests: [
        { },
        { extractable: false },
        { derivedAlgorithm: { name: "AES-CBC", length: 256 } },
        { derivedAlgorithm: { name: "AES-CTR", length: 256 } },
        { derivedAlgorithm: { name: "AES-GCM", length: 256 } },
        { derivedAlgorithm: { name: "HMAC", hash: "SHA-256", length: 256 },
          usage: ["sign", "verify"] },

        { pair: ['ec_secp384r1', 'ec2_secp384r1'],
          expected: "4OmeRzZZ53eCgn09zI2TumH4n4Zp-nfHsfZTOBEu8Hg" },
        { pair: ['ec_secp384r1', 'ec2_secp384r1'], extractable: false },

        { pair: ['ec_secp521r1', 'ec_secp384r1'],
          exception: "TypeError: ECDH keys must use the same curve" },

        { pair: ['ec_secp521r1', 'ec2_secp521r1'],
          derivedAlgorithm: { name: "AES-GCM", length: 128 },
          expected: "ATBls20ukLQI7AJQ6LRnyA" },
        { pair: ['ec_secp521r1', 'ec2_secp521r1'],
          derivedAlgorithm: { name: "HMAC", hash: "SHA-384", length: 512 },
          expected: "ATBls20ukLQI7AJQ6LRnyD6wLDR_FDmBoAdVX5_DB_bMDe_uYMjN-jQqPTkGNIo6NOqmXMX9KNQ-AqL8aPjySA",
          usage: ["sign", "verify"] }
    ]
};

async function testDerivedKeyType(params) {
    let pair = await load_key_pair('ec');
    let peer = await load_key_pair('ec2');
    let ecdh = { name: "ECDH", public: peer.publicKey };
    let derived = { name: params.name, length: 256 };

    if (params.name === "ECDSA") {
        derived.namedCurve = "P-256";
    }

    let key;

    try {
        key = await crypto.subtle.deriveKey(ecdh, pair.privateKey, derived,
                                            false, params.usage);

    } catch (e) {
        if (params.reject
            && (e.name === "TypeError" || e.name === "NotSupportedError"))
        {
            return "SUCCESS";
        }

        throw e;
    }

    if (params.reject) {
        throw Error("deriveKey accepted an asymmetric derived key");
    }

    let bits = await crypto.subtle.deriveBits(ecdh, pair.privateKey, 256);
    let imported = await crypto.subtle.importKey("raw", bits, params.name,
                                                 false, ["deriveBits"]);
    let alg = { name: params.name, hash: "SHA-256",
                salt: new Uint8Array(16), info: new Uint8Array(0),
                iterations: 1 };

    let a = await crypto.subtle.deriveBits(alg, key, 128);
    let b = await crypto.subtle.deriveBits(alg, imported, 128);

    if (Buffer.from(a).toString("hex") !== Buffer.from(b).toString("hex")) {
        throw Error(`${params.name} derived key does not match deriveBits`);
    }

    return "SUCCESS";
}

/* Error names other engines use for invalid derived key parameters. */
const invalid = ["TypeError", "OperationError", "NotSupportedError",
                 "DataError", "SyntaxError"];

async function testDerivedKeyLength(params) {
    let ecdh_pair = await load_key_pair('ec');
    let peer = await load_key_pair('ec2');
    let base;

    if (params.base === "ECDH") {
        base = { algorithm: { name: "ECDH", public: peer.publicKey },
                 key: ecdh_pair.privateKey };

    } else {
        base = {
            algorithm: { name: params.base, hash: "SHA-256",
                         salt: new Uint8Array(16), info: new Uint8Array(0),
                         iterations: 1 },
            key: await crypto.subtle.importKey("raw", new Uint8Array(32),
                                               params.base, false,
                                               ["deriveKey"]),
        };
    }

    let key;

    try {
        key = await crypto.subtle.deriveKey(base.algorithm, base.key,
                                            params.derived, true,
                                            params.usage);

    } catch (e) {
        if (params.error && params.error.indexOf(e.name) !== -1) {
            return "SUCCESS";
        }

        throw e;
    }

    if (params.error) {
        throw Error(`deriveKey accepted ${JSON.stringify(params.derived)}`);
    }

    let raw = await crypto.subtle.exportKey("raw", key);

    if (raw.byteLength !== params.bytes) {
        throw Error(`unexpected key length: ${raw.byteLength}`);
    }

    return "SUCCESS";
}

let derived_key_length_tsuite = {
    name: "deriveKey length",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: testDerivedKeyLength,
    opts: {
        usage: ["sign"]
    },
    tests: [
        { base: "ECDH", derived: { name: "AES-GCM", length: 129 },
          usage: ["encrypt"],
          error: invalid },
        { base: "HKDF", derived: { name: "AES-GCM", length: 129 },
          usage: ["encrypt"],
          error: invalid },
        { base: "PBKDF2", derived: { name: "AES-GCM", length: 129 },
          usage: ["encrypt"],
          error: invalid },
        { base: "ECDH",
          derived: { name: "HMAC", hash: "SHA-256", length: 7 },
          error: invalid },
        { base: "HKDF",
          derived: { name: "HMAC", hash: "SHA-256", length: 0 },
          error: invalid },
        { base: "ECDH",
          derived: { name: "HMAC", hash: "SHA-256", length: 8 },
          bytes: 1 },
        { base: "HKDF",
          derived: { name: "ECDSA", namedCurve: "P-256", length: 256 },
          error: invalid },
    ]
};

let ecdh_key_type_tsuite = {
    name: "ECDH-DeriveKey type",
    skip: () => (!has_buffer() || !has_webcrypto()),
    T: testDerivedKeyType,
    opts: {
        usage: ["deriveBits"]
    },
    tests: [
        { name: "HKDF" },
        { name: "PBKDF2" },
        { name: "ECDSA", usage: ["sign"], reject: true },
    ]
};

run([
    ecdh_bits_tsuite,
    ecdh_key_tsuite,
    ecdh_key_type_tsuite,
    derived_key_length_tsuite,
])
.then($DONE, $DONE);
