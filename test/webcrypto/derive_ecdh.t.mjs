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

run([
    ecdh_bits_tsuite,
    ecdh_key_tsuite,
])
.then($DONE, $DONE);
