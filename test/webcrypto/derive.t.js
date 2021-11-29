/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    let r;
    let encoder = new TextEncoder();
    let keyMaterial = await crypto.subtle.importKey("raw", encoder.encode(params.pass),
                                                    params.algorithm.name,
                                                    false, [ "deriveBits", "deriveKey" ]);
    if (params.derive === "key") {
        let key = await crypto.subtle.deriveKey(params.algorithm, keyMaterial,
                                                params.derivedAlgorithm,
                                                true, [ "encrypt", "decrypt" ]);

        r = await crypto.subtle.encrypt(params.derivedAlgorithm, key,
                                        encoder.encode(params.text));
    } else {

        r = await crypto.subtle.deriveBits(params.algorithm, keyMaterial, params.length);
    }

    r = Buffer.from(r).toString("hex");

    if (params.expected != r) {
        throw Error(`${params.algorithm.name} failed expected: "${params.expected}" vs "${r}"`);
    }

    return "SUCCESS";
}

function p(args, default_opts) {
    let params = Object.assign({}, default_opts);
    params = merge(params, args);

    params.algorithm.salt = Buffer.from(params.algorithm.salt, "hex");
    params.algorithm.info = Buffer.from(params.algorithm.info, "hex");
    params.derivedAlgorithm.iv = Buffer.from(params.derivedAlgorithm.iv, "hex");

    return params;
}

let derive_tsuite = {
    name: "derive",
    skip: () => (!has_fs() || !has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        text: "secReT",
        pass: "passW0rd",
        derive: "key",
        optional: false,
        length: 256,
        algorithm: {
            name: "PBKDF2",
            salt: "00112233001122330011223300112233",
            hash: "SHA-256",
            info: "deadbeef",
            iterations: 100000
        },
        derivedAlgorithm: {
          name: "AES-GCM",
          length: 256,
          iv: "55667788556677885566778855667788"
        }
    },

    tests: [
        { expected: "e7b55c9f9fda69b87648585f76c58109174aaa400cfa" },
        { pass: "pass2", expected: "e87d1787f2807ea0e1f7e1cb265b23004c575cf2ad7e" },
        { algorithm: { iterations: 10000 }, expected: "5add0059931ed1db1ca24c26dbe4de5719c43ed18a54" },
        { algorithm: { hash: "SHA-512" }, expected: "544d64e5e246fdd2ba290ea932b2d80ef411c76139f4" },
        { algorithm: { salt: "aabbccddaabbccddaabbccddaabbccdd" }, expected: "5c1304bedf840b1f6f7d1aa804fe870a8f949d762c32" },
        { algorithm: { salt: "aabbccddaabbccddaabbccddaabb" },
          exception: "TypeError: PBKDF2 algorithm.salt must be at least 16 bytes long" },
        { derivedAlgorithm: { length: 128 }, expected: "9e2d7bcc1f21f30ec3c32af9129b64507d086d129f2a" },
        { derivedAlgorithm: { length: 32 },
          exception: "TypeError: deriveKey \"AES-GCM\" length must be 128 or 256" },
        { derivedAlgorithm: { name: "AES-CBC" }, expected: "3ad6523692d44b6a7a90be7c2721786f" },

        { derive: "bits", expected: "6458ed6e16b998d4e646422171087be8a1ee34bed463dfcb3dcd30842b1228fe" },
        { derive: "bits", pass: "pass2", expected: "ef8f75073fcadfd504d26610c743873e297ad90340c23ddc0e5f6bdb83cbabb2" },
        { derive: "bits", algorithm: { salt: "aabbccddaabbccddaabbccddaabbccdd" },
          expected: "22ceb295aa25b59c6bc5b383a089bd6999006c03f273ce3614a4fa0d90bd29ae" },
        { derive: "bits", algorithm: { hash: "SHA-1" },
          expected: "a2fc83498f7d07b4c8180c7ebfec2af0f3a7d6cb08bf8593d41d3c5c1e1c4d67" },
        { derive: "bits", algorithm: { hash: "SHA-1" }, length: 128,
          expected: "a2fc83498f7d07b4c8180c7ebfec2af0" },
        { derive: "bits", algorithm: { hash: "SHA-1" }, length: 64,
          expected: "a2fc83498f7d07b4" },

        { algorithm: { name: "HKDF" }, optional: true,
          expected: "18ea069ee3317d2db02e02f4a228f50dc80d9a2396e6" },
        { derive: "bits", algorithm: { name: "HKDF" }, optional: true,
          expected: "e089c7491711306c69e077aa19fae6bfd2d4a6d240b0d37317d50472d7291a3e" },
]};

run([derive_tsuite])
.then($DONE, $DONE);
