/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    let encoder = new TextEncoder();
    let sign_key = await crypto.subtle.importKey(params.sign_key.fmt,
                                                 params.sign_key.key,
                                                 params.import_alg,
                                                 false, [ "sign" ]);

    let sig = await crypto.subtle.sign(params.sign_alg, sign_key,
                                     encoder.encode(params.text))
                    .catch (e => {
                        if (e.toString().startsWith("Error: EVP_PKEY_CTX_set_signature_md() failed")) {
                            /* Red Hat Enterprise Linux: SHA-1 is disabled */
                            return "SKIPPED";
                        }
                    });

    if (sig == "SKIPPED") {
        return sig;
    }

    if (params.verify) {
        let verify_key = await crypto.subtle.importKey(params.verify_key.fmt,
                                                       params.verify_key.key,
                                                       params.import_alg,
                                                       false, [ "verify" ]);

        let r = await crypto.subtle.verify(params.sign_alg, verify_key, sig,
                                           encoder.encode(params.text));

        if (params.expected !== r) {
            throw Error(`${params.sign_alg.name} failed expected: "${params.expected}" vs "${r}"`);
        }

        if (params.expected === true) {
            let broken_sig = Buffer.concat([Buffer.from(sig)]);
            broken_sig[8] = 255 - broken_sig[8];

            r = await crypto.subtle.verify(params.sign_alg, verify_key, broken_sig,
                                           encoder.encode(params.text));
            if (r !== false) {
                throw Error(`${params.sign_alg.name} BROKEN SIG failed expected: "false" vs "${r}"`);
            }

            let broken_text = encoder.encode(params.text);
            broken_text[0] = 255 - broken_text[0];

            r = await crypto.subtle.verify(params.sign_alg, verify_key, sig,
                                           broken_text);
            if (r !== false) {
                throw Error(`${params.sign_alg.name} BROKEN TEXT failed expected: "false" vs "${r}"`);
            }
        }

    } else {
        sig = Buffer.from(sig).toString("hex");

        if (params.expected !== sig) {
            throw Error(`${params.sign_alg.name} failed expected: "${params.expected}" vs "${sig}"`);
        }
    }


    return "SUCCESS";
}

function p(args, default_opts) {
    let key;
    let encoder = new TextEncoder();
    let params = merge({}, default_opts);
    params = merge(params, args);

    switch (params.sign_key.fmt) {
    case "pkcs8":
        let pem = fs.readFileSync(`test/webcrypto/${params.sign_key.key}`);
        key = pem_to_der(pem, "PRIVATE");
        break;
    case "raw":
        key = encoder.encode(params.sign_key.key);
        break;
    default:
        throw Error("Unknown sign key format");
    }

    params.sign_key.key = key;

    switch (params.verify_key.fmt) {
    case "spki":
        let pem = fs.readFileSync(`test/webcrypto/${params.verify_key.key}`);
        key = pem_to_der(pem, "PUBLIC");
        break;
    case "raw":
        key = encoder.encode(params.verify_key.key);
        break;
    default:
        throw Error("Unknown verify key format");
    }

    params.verify_key.key = key;

    return params;
}

let hmac_tsuite = {
    name: "HMAC sign",
    skip: () => (!has_fs() || !has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        text: "TExt-T0-SiGN",
        sign_key: { key: "secretKEY", fmt: "raw" },
        verify_key: { key: "secretKEY", fmt: "raw" },
        verify: false,
        import_alg: {
            name: "HMAC",
            hash: "SHA-256",
        },
        sign_alg: {
            name: "HMAC",
        },
    },

    tests: [
        { expected: "76d4f1b22d7544c34e86380c9ab7c756311810dc31e4af3b705045d263db1212" },
        { import_alg: { hash: "SHA-384" },
          expected: "4bdaa7e80868a9cda35ad78ae5d88c29f1ff97680317c5bc3df1deccf2dad0cf3edce945ed90ec53fa48d887a04d4963" },
        { import_alg: { hash: "SHA-512" },
          expected: "9dd589ae5e75b6fb8d453c072cc05e6f5eb3d29034d3a0df2559ffe158f3f99fef98a9d1ab2fca459cceea0be3cb7aa3269d77fc9382b56a9cd0571851339938" },
        { import_alg: { hash: "SHA-1" },
          expected: "0540c587e7ee607fb4fd5e814438ed50f261c244" },
        { sign_alg: { name: "ECDSA" }, exception: "TypeError: cannot sign using \"HMAC\" with \"ECDSA\" key" },

        { verify: true, expected: true },
        { verify: true, import_alg: { hash: "SHA-384" }, expected: true },
        { verify: true, import_alg: { hash: "SHA-512" }, expected: true },
        { verify: true, import_alg: { hash: "SHA-1" }, expected: true },
        { verify: true, verify_key: { key: "secretKEY2" }, expected: false },
]};

let rsassa_pkcs1_v1_5_tsuite = {
    name: "RSASSA-PKCS1-v1_5 sign",
    skip: () => (!has_fs() || !has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        text: "TExt-T0-SiGN",
        sign_key: { key: "rsa.pkcs8", fmt: "pkcs8" },
        verify_key: { key: "rsa.spki", fmt: "spki" },
        import_alg: {
            name: "RSASSA-PKCS1-v1_5",
            hash: "SHA-256",
        },
        sign_alg: {
            name: "RSASSA-PKCS1-v1_5",
        },
    },

    tests: [
        { expected: "b126c528abd305dc2b7234de44ffa2190bd55f57087f75620196e8bdb05ba205e52ceca03e4799f30a6d61a6610878b1038a5dd869ab8c04ffe80d49d14407b2c2fe52ca78c9c409fcf7fee26188941f5072179c2bf2de43e637b089c32cf04f14ca01e7b9c33bbbec603b2815de0180b12a3269b0453aba158642e00303890d" },
        { import_alg: { hash: "SHA-512" },
          expected: "174adca014132f5b9871e1bda2c23fc50f57673c6915b9170d601c626022a03d66c1b8c2a4b8efa08edee83ad27cc05c0d33c7a52a9125fa5be0f99be40483d8123570f91d53f2af51ef0f2b43987182fd114db242f146ea0d7c4ead5d4a11043f83e67d5400fc66dc2b08d7d63122fcd11b495fb4115ecf57c51994f6c516b9" },
        { import_alg: { hash: "SHA-1" },
          expected: "0cc6377ae31a1b09a7c0a18d12e785e9734565bdeb808b3e41d8bc03adab9ffbd8b1764830fea8f1d8f327034f24296f3aad6112cc3a380db6ef01989f8f9cb608f75b1d9558c36785b6f932ee06729b139b5f02bb886fd1d4fb0f06246064993a421e55579c490c77c27a44c7cc0ea7dd6579cc69402177712ba0f69cac967d" },

        { verify: true, expected: true },
        { verify: true, import_alg: { hash: "SHA-512" }, expected: true },
        { verify: true, import_alg: { hash: "SHA-1" }, expected: true },
        { verify: true, verify_key: { key: "rsa2.spki" }, expected: false },
]};

let rsa_pss_tsuite = {
    name: "RSA-PSS sign",
    skip: () => (!has_fs() || !has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        text: "TExt-T0-SiGN",
        sign_key: { key: "rsa.pkcs8", fmt: "pkcs8" },
        verify_key: { key: "rsa.spki", fmt: "spki" },
        import_alg: {
            name: "RSA-PSS",
            hash: "SHA-256",
        },
        sign_alg: {
            name: "RSA-PSS",
            saltLength: 0,
        },
    },

    tests: [
        { expected: "c126f05ea6e13b3208540bd833f5886d95fe2c89f9b3102b564c9da3bc0c00d224e6ed9be664dee61dfcc0eee790f816c5cf6a0ffc320112d818b72d57de9adbb31d239c225d42395c906bde719bf4ad21c18c679d70186d2efc044fc4995773c5085c64c6d9b7a5fc96dd28176e2cd702a9f35fe64b960f21523ec19bb44408" },
        { import_alg: { hash: "SHA-512" }, expected: "3764287839843d25cb8ad109d0ffffd54a8f47fae02e9d2fa8a9363a7b0f98d0ede417c57c0d99a8c11cd502bbc95767a5f437b99cb30341c7af840889633e08cfdaae472bed3e68d451c67182ccd583457c6a9cf81c7e17fb391606f1bc02a83253975f153582ca1c31e9ba9b89dec4bf1d2a9b7b5024dd4dde317432ff26b1" },
        { import_alg: { hash: "SHA-1" }, expected: "73d39d22b028b13142b257d405a4a09d0622b97ef7b74e0953274744a76fedee0f283b678cfcaa8e4c38ef84033259f84c59ae987f9d049adea4379a9b0addb9f8b53ee6b64a4e32d8165d057444a1056706da648b88c6a4613022e03be5b6b9e8948d9527a95478f871bfe88dbc67127b038520af3400b942c85e0733bcad27" },

        { verify: true, expected: true },
        { verify: true, import_alg: { hash: "SHA-512" }, expected: true },
        { verify: true, sign_alg: { saltLength: 32 }, expected: true },
        { verify: true, import_alg: { hash: "SHA-512" }, sign_alg: { saltLength: 32 },
          expected: true },
        { verify: true, verify_key: { key: "rsa2.spki" }, expected: false },
]};

let ecdsa_tsuite = {
    name: "ECDSA sign",
    skip: () => (!has_fs() || !has_buffer() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        text: "TExt-T0-SiGN",
        sign_key: { key: "ec.pkcs8", fmt: "pkcs8" },
        verify_key: { key: "ec.spki", fmt: "spki" },
        import_alg: {
            name: "ECDSA",
            namedCurve: "P-256",
        },
        sign_alg: {
            name: "ECDSA",
            hash: "SHA-256",
        },
    },

    tests: [
        { verify: true, expected: true },
        { verify: true, import_alg: { hash: "SHA-384" }, expected: true },
        { verify: true, import_alg: { hash: "SHA-512" }, expected: true },
        { verify: true, import_alg: { hash: "SHA-1" }, expected: true },
        { verify: true, verify_key: { key: "ec2.spki" }, expected: false },
        { verify: true, verify_key: { key: "rsa.spki" }, exception: "Error: EC key is not found" },
        { verify: true, import_alg: { namedCurve: "P-384" }, exception: "Error: name curve mismatch" },
]};

run([
    hmac_tsuite,
    rsassa_pkcs1_v1_5_tsuite,
    rsa_pss_tsuite,
    ecdsa_tsuite
])
.then($DONE, $DONE);
