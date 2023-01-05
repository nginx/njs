/*---
includes: [compatFs.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js, compareObjects.js]
flags: [async]
---*/

async function load_key(params) {
    if (params.generate_keys) {
        let type = params.generate_keys.type;
        if (params.generate_keys.keys) {
            return params.generate_keys.keys[type];
        }

        params.generate_keys.keys = await crypto.subtle.generateKey(params.generate_keys.alg,
                                                                    params.generate_keys.extractable,
                                                                    params.generate_keys.usage);

        return params.generate_keys.keys[type];
    }

    return await crypto.subtle.importKey(params.key.fmt,
                                         params.key.key,
                                         params.key.alg,
                                         params.key.extractable,
                                         params.key.usage);

}

async function test(params) {
    let key = await load_key(params);
    let exp = await crypto.subtle.exportKey(params.export.fmt, key);

    if (params.check && !params.check(exp, params)) {
        throw Error(`failed check`);
    }

    if (exp[Symbol.toStringTag] == 'ArrayBuffer') {
        let buf = Buffer.from(exp);
        exp = "ArrayBuffer:" + buf.toString('base64url');
    }

    if (params.expected && !compareObjects(params.expected, exp)) {
        throw Error(`unexpected export key: ${JSON.stringify(exp)}\n expected: ${JSON.stringify(params.expected)}`);
    }

    if (!params.generate_keys
        && (exp.startsWith && !exp.startsWith("ArrayBuffer:")))
    {
        /* Check that exported key can be imported back. */
        let imported = await crypto.subtle.importKey(params.export.fmt,
                                                     exp,
                                                     params.key.alg,
                                                     params.key.extractable,
                                                     params.key.usage);
    }

    return 'SUCCESS';
}

function p(args, default_opts) {
    let key, pem;
    let params = merge({}, default_opts);
    params = merge(params, args);

    switch (params.key.fmt) {
    case "spki":
        pem = fs.readFileSync(`test/webcrypto/${params.key.key}`);
        key = pem_to_der(pem, "PUBLIC");
        break;
    case "pkcs8":
        pem = fs.readFileSync(`test/webcrypto/${params.key.key}`);
        key = pem_to_der(pem, "PRIVATE");
        break;
    case "jwk":
        key = load_jwk(params.key.key);
        break;
    default:
        throw Error("Unknown encoding key format");
    }

    params.key.key = key;

    return params;
}

function validate_property(exp, p, exp_len) {
    if (!exp[p]) {
        throw Error(`"${p}" is not found in ${JSON.stringify(exp)}`);
    }

    if (typeof exp[p] != 'string') {
        throw Error(`"${p}" is not a string`);
    }

    let len = exp[p].length;

    if (len < exp_len - 4 || len > exp_len + 4) {
        throw Error(`"${p}":"${exp[p]}" length is out of range [${exp_len - 4}, ${exp_len + 4}]`);
    }
}


function validate_rsa_jwk(exp, params) {
    let expected_len = params.generate_keys.alg.modulusLength / 8 * (4 / 3);
    expected_len = Math.round(expected_len);

    validate_property(exp, 'n', expected_len);

    if (params.generate_keys.type == 'privateKey') {
        validate_property(exp, 'd', expected_len);
        validate_property(exp, 'p', expected_len / 2);
        validate_property(exp, 'q', expected_len / 2);

        validate_property(exp, 'dq', expected_len / 2);
        validate_property(exp, 'dp', expected_len / 2);
        validate_property(exp, 'qi', expected_len / 2);
    }

    return true;
}

let rsa_tsuite = {
    name: "RSA exporting",
    skip: () => (!has_fs() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        key: {
               fmt: "spki",
               key: "rsa.spki",
               alg: { name: "RSA-OAEP", hash: "SHA-256" },
               extractable: true,
               usage: [ "encrypt" ]
        },
        export: { fmt: "jwk" },
        expected: {
            ext: true,
            kty: "RSA",
            e: "AQAB",
        },
    },

    tests: [
      { expected: { key_ops: [ "encrypt" ],
                    n: "yUmxoJC8VAM5hyYZa-XUBZg1N1ywFMPUpWsF1kaSGed98P3XUgPzgX80wpyzd5qdGuALqnf2lMc7O8PrGBtO5YrvQlI96NX0jUo5bc5wz220ob3AUCeQnTfx-UFqM4pCwjoDSo2PlphJdWgFYymGBaBCJgnENQL9H1N_8_yNiN8",
                    alg: "RSA-OAEP-256" } },
      { export: { fmt: "spki" },
        expected: "ArrayBuffer:MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDJSbGgkLxUAzmHJhlr5dQFmDU3XLAUw9SlawXWRpIZ533w_ddSA_OBfzTCnLN3mp0a4Auqd_aUxzs7w-sYG07liu9CUj3o1fSNSjltznDPbbShvcBQJ5CdN_H5QWozikLCOgNKjY-WmEl1aAVjKYYFoEImCcQ1Av0fU3_z_I2I3wIDAQAB" },

      { key: { fmt: "pkcs8", key: "rsa.pkcs8", usage: [ "decrypt" ], alg: { hash: "SHA-512" } },
        expected: { key_ops: [ "decrypt" ],
                    n: "yUmxoJC8VAM5hyYZa-XUBZg1N1ywFMPUpWsF1kaSGed98P3XUgPzgX80wpyzd5qdGuALqnf2lMc7O8PrGBtO5YrvQlI96NX0jUo5bc5wz220ob3AUCeQnTfx-UFqM4pCwjoDSo2PlphJdWgFYymGBaBCJgnENQL9H1N_8_yNiN8",
                    alg: "RSA-OAEP-512" } },

      { key: { fmt: "pkcs8", key: "rsa.pkcs8", usage: [ "decrypt" ], alg: { hash: "SHA-512" } },
        export: { fmt: "pkcs8" },
        expected: "ArrayBuffer:MIICeAIBADANBgkqhkiG9w0BAQEFAASCAmIwggJeAgEAAoGBAMlJsaCQvFQDOYcmGWvl1AWYNTdcsBTD1KVrBdZGkhnnffD911ID84F_NMKcs3eanRrgC6p39pTHOzvD6xgbTuWK70JSPejV9I1KOW3OcM9ttKG9wFAnkJ038flBajOKQsI6A0qNj5aYSXVoBWMphgWgQiYJxDUC_R9Tf_P8jYjfAgMBAAECgYEAj06DQyCopFujYoASi0oWmGEUSjUYO8BsrdSzVCnsLLsuZBwlZ4Peouyw4Hl2IIoYniCyzYwZJzVtC5Dh2MjgcrJTG5nX3FfheuabGl4in0583C51ZYWlVpDvBWw8kJTfXjiKH4z6ZA9dWdT5Y3aH_kOf-znUc7eTvuzISs61x_kCQQD0BJvbLDlvx3u6esW47LLgQNw9ufMSlu5UYBJ4c-qQ5HAeyp4Zt_AaWENhJitjQcLBSxIFIVw7dIN67RnTNK8VAkEA0yvzzgHo_PGYSlVj-M3965AwQF2wTXz82MZHv6EfcCHKuBfCSecr-igqLHhzfynAQjjf39VrXuPuRL23REF1IwJBAKVFydo0peJTljXDmc-aYb0JsSINo9jfaSS0vU3gFOt2DYqNaW-56WGujlRqadCcZbBNjDL1WWbbj4HevTMT59ECQEWaKgzPolykwN5XUNE0DCp1ZwIAH1kbBjfo-sMVt0f9S1TsN9SmBl-4l1X7CY5zU3RATMH5FR-8ns83fM1ZieMCQQDZEQ-dFAhouzJrnCXAXDTCHA9oBtNmnaN-C6G2DmCi79iu7sLHP9vzdgU-CgjrG4YTU5exaRFNOhLwW4hYKs0F" },

      { key: { fmt: "pkcs8", key: "rsa.pkcs8", usage: [ "decrypt" ], alg: { hash: "SHA-512" } },
        export: { fmt: "spki" },
        exception: "TypeError: private key of \"RSA-OAEP\" cannot be exported as SPKI" },

      { generate_keys: { alg: { name: "RSA-OAEP",
                                modulusLength: 2048,
                                publicExponent: new Uint8Array([1, 0, 1]),
                                hash: "SHA-256" },
                         extractable: true,
                         type: "publicKey",
                         usage: [ "encrypt", "decrypt" ] },
        check: validate_rsa_jwk,
        expected: { kty: "RSA", ext: true, key_ops: [ "encrypt" ], e: "AQAB", alg: "RSA-OAEP-256" } },
      { generate_keys: { alg: { name: "RSA-OAEP",
                                modulusLength: 1024,
                                publicExponent: new Uint8Array([1, 0, 1]),
                                hash: "SHA-1" },
                         extractable: true,
                         type: "privateKey",
                         usage: [ "encrypt", "decrypt" ] },
        check: validate_rsa_jwk,
        expected: { kty: "RSA", ext: true, key_ops: [ "decrypt" ], e: "AQAB", alg: "RSA-OAEP" } },
      { generate_keys: { alg: { name: "RSA-OAEP",
                                modulusLength: 1024,
                                publicExponent: new Uint8Array([1, 0, 1]),
                                hash: "SHA-1" },
                         extractable: false,
                         type: "privateKey",
                         usage: [ "encrypt", "decrypt" ] },
        check: validate_rsa_jwk,
        exception: "TypeError: provided key cannot be extracted" },
      { generate_keys: { alg: { name: "RSASSA-PKCS1-v1_5",
                                modulusLength: 1024,
                                publicExponent: new Uint8Array([1, 0, 1]),
                                hash: "SHA-512" },
                         extractable: true,
                         type: "publicKey",
                         usage: [ "sign", "verify" ] },
        check: validate_rsa_jwk,
        expected: { kty: "RSA", ext: true, key_ops: [ "verify" ], e: "AQAB", alg: "RS512" } },
      { generate_keys: { alg: { name: "RSASSA-PKCS1-v1_5",
                                modulusLength: 1024,
                                publicExponent: new Uint8Array([1, 0, 1]),
                                hash: "SHA-256" },
                         extractable: true,
                         type: "privateKey",
                         usage: [ "sign", "verify" ] },
        check: validate_rsa_jwk,
        expected: { kty: "RSA", ext: true, key_ops: [ "sign" ], e: "AQAB", alg: "RS256" } },
      { generate_keys: { alg: { name: "RSA-PSS",
                                modulusLength: 1024,
                                publicExponent: new Uint8Array([1, 0, 1]),
                                hash: "SHA-384" },
                         extractable: true,
                         type: "publicKey",
                         usage: [ "sign", "verify" ] },
        check: validate_rsa_jwk,
        expected: { kty: "RSA", ext: true, key_ops: [ "verify" ], e: "AQAB", alg: "PS384" } },
      { generate_keys: { alg: { name: "RSA-PSS",
                                modulusLength: 1024,
                                publicExponent: new Uint8Array([1, 0, 1]),
                                hash: "SHA-1" },
                         extractable: true,
                         type: "privateKey",
                         usage: [ "sign", "verify" ] },
        check: validate_rsa_jwk,
        expected: { kty: "RSA", ext: true, key_ops: [ "sign" ], e: "AQAB", alg: "PS1" } },
]};

function validate_ec_jwk(exp, params) {
    let crv = params.generate_keys.alg.namedCurve;
    let expected_len = Number(crv.slice(2)) / 8 * (4 / 3);
    expected_len = Math.round(expected_len);

    validate_property(exp, 'x', expected_len);
    validate_property(exp, 'y', expected_len);

    if (params.generate_keys.type == 'privateKey') {
        validate_property(exp, 'd', expected_len);
    }

    return true;
}

let ec_tsuite = {
    name: "EC exporting",
    skip: () => (!has_fs() || !has_webcrypto()),
    T: test,
    prepare_args: p,
    opts: {
        key: { fmt: "spki",
               key: "ec.spki",
               alg: { name: "ECDSA", namedCurve: "P-256" },
               extractable: true,
               usage: [ "verify" ] },
        export: { fmt: "jwk" },
        expected: { ext: true, kty: "EC" },
    },

    tests: [
      { expected: { key_ops: [ "verify" ],
                    x: "cUUsZRGuVYReGiTQl9MqR7ir6dZIgw-8pM-F0phJWdw",
                    y: "4Nzn_7uNz0AA3U4fhpfVxSD4U5QciGyEoM4r3jC7bjI",
                    crv: "P-256" } },
      { key: { fmt: "pkcs8", key: "ec.pkcs8", usage: [ "sign" ] },
        expected: { key_ops: [ "sign" ],
                    x: "cUUsZRGuVYReGiTQl9MqR7ir6dZIgw-8pM-F0phJWdw",
                    y: "4Nzn_7uNz0AA3U4fhpfVxSD4U5QciGyEoM4r3jC7bjI",
                    d: "E2sW0_4a3QXaSTJ0JKbSUbieKTD1UFtr7i_2CuetP6A",
                    crv: "P-256" } },
      { key: { fmt: "pkcs8", key: "ec.pkcs8", usage: [ "sign" ] },
        export: { fmt: "pkcs8" },
        expected: "ArrayBuffer:MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgE2sW0_4a3QXaSTJ0JKbSUbieKTD1UFtr7i_2CuetP6ChRANCAARxRSxlEa5VhF4aJNCX0ypHuKvp1kiDD7ykz4XSmElZ3ODc5_-7jc9AAN1OH4aX1cUg-FOUHIhshKDOK94wu24y" },
      { export: { fmt: "pkcs8" },
        exception: "TypeError: public key of \"ECDSA\" cannot be exported as PKCS8" },
      { export: { fmt: "raw" },
        expected: "ArrayBuffer:BHFFLGURrlWEXhok0JfTKke4q-nWSIMPvKTPhdKYSVnc4Nzn_7uNz0AA3U4fhpfVxSD4U5QciGyEoM4r3jC7bjI" },
      { key: { fmt: "pkcs8", key: "ec.pkcs8", usage: [ "sign" ] },
        export: { fmt: "raw" },
        exception: "TypeError: private key of \"ECDSA\" cannot be exported in \"raw\" format" },
      { generate_keys: { alg: { name: "ECDSA",
                                namedCurve: "P-256" },
                         extractable: true,
                         type: "publicKey",
                         usage: [ "sign", "verify" ] },
        check: validate_ec_jwk,
        expected: { kty: "EC", ext: true, key_ops: [ "verify" ], crv: "P-256" } },
      { generate_keys: { alg: { name: "ECDSA",
                                namedCurve: "P-384" },
                         extractable: true,
                         type: "privateKey",
                         usage: [ "sign", "verify" ] },
        check: validate_ec_jwk,
        expected: { kty: "EC", ext: true, key_ops: [ "sign" ], crv: "P-384" } },
]};

run([
    rsa_tsuite,
    ec_tsuite,
])
.then($DONE, $DONE);
