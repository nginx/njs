const fs = require('fs');

if (typeof crypto == 'undefined') {
    crypto = require('crypto').webcrypto;
}

async function run(tlist) {
    function validate(t, r, i) {
        if (r.status == "fulfilled" && !t[i].exception) {
            return r.value === "SUCCESS";
        }

        if (r.status == "rejected" && t[i].exception) {
            if (process.argv[2] === '--match-exception-text') {
                /* is not compatible with node.js format */
                return r.reason.toString().startsWith(t[i].exception);
            }

            return true;
        }

        return false;
    }

    for (let k = 0; k < tlist.length; k++) {
        let ts = tlist[k];
        let results = await Promise.allSettled(ts.tests.map(t => ts.T(ts.prepare_args(t, ts.opts))));
        let r = results.map((r, i) => validate(ts.tests, r, i));

        console.log(`${ts.name} ${r.every(v=>v == true) ? "SUCCESS" : "FAILED"}`);

        r.forEach((v, i) => {
            if (!v) {
                console.log(`FAILED ${i}: ${JSON.stringify(ts.tests[i])}\n    with reason: ${results[i].reason}`);
            }
        })
    }
}

function pem_to_der(pem, type) {
    const pemJoined = pem.toString().split('\n').join('');
    const pemHeader = `-----BEGIN ${type} KEY-----`;
    const pemFooter = `-----END ${type} KEY-----`;
    const pemContents = pemJoined.substring(pemHeader.length, pemJoined.length - pemFooter.length);
    return Buffer.from(pemContents, 'base64');
}

function p(args, default_opts) {
    let params = Object.assign({}, default_opts, args);

    params.data = Buffer.from(params.data, "hex");

    return params;
}

async function test(params) {
    let spki = await crypto.subtle.importKey("spki",
                            pem_to_der(fs.readFileSync(`test/webcrypto/${params.spki}`), "PUBLIC"),
                            {name:"RSA-OAEP", hash:params.spki_hash},
                            false, ["encrypt"]);

    let pkcs8 = await crypto.subtle.importKey("pkcs8",
                            pem_to_der(fs.readFileSync(`test/webcrypto/${params.pkcs8}`), "PRIVATE"),
                            {name:"RSA-OAEP", hash:params.pkcs8_hash},
                            false, ["decrypt"]);

    let enc = await crypto.subtle.encrypt({name: "RSA-OAEP"}, spki, params.data);

    let plaintext = await crypto.subtle.decrypt({name: "RSA-OAEP"}, pkcs8, enc);

    plaintext = Buffer.from(plaintext);

    if (params.data.compare(plaintext) != 0) {
        throw Error(`RSA-OAEP encoding/decoding failed expected: "${params.data}" vs "${plaintext}"`);
    }

    return 'SUCCESS';
};

let rsa_tsuite = {
    name: "RSA-OAEP encoding/decoding",
    T: test,
    prepare_args: p,
    opts: {
        spki: "rsa.spki",
        spki_hash: "SHA-256",
        pkcs8: "rsa.pkcs8",
        pkcs8_hash: "SHA-256",
    },

    tests: [
        { data: "aabbcc" },
        { data: "aabbccdd".repeat(4) },
        { data: "aabbccdd".repeat(7) },
        { data: "aabbcc", spki_hash: "SHA-1", pkcs8_hash: "SHA-1" },
        { data: "aabbccdd".repeat(4), spki_hash: "SHA-1", pkcs8_hash: "SHA-1" },
        { data: "aabbccdd".repeat(7), spki_hash: "SHA-1", pkcs8_hash: "SHA-1" },
        { data: "aabbcc", spki_hash: "SHA-384", pkcs8_hash: "SHA-384" },
        { data: "aabbccdd".repeat(4), spki_hash: "SHA-384", pkcs8_hash: "SHA-384" },
        { data: "aabbccdd".repeat(7), spki_hash: "SHA-384", pkcs8_hash: "SHA-384" },

        { data: "aabbcc", spki_hash: "SHA-256", pkcs8_hash: "SHA-384", exception: "Error: EVP_PKEY_decrypt() failed" },
        { data: "aabbcc", spki_hash: "XXX", exception: "TypeError: unknown hash name: \"XXX\"" },
        { data: "aabbcc", spki: "rsa.spki.broken", exception: "Error: d2i_PUBKEY() failed" },
        { data: "aabbcc", spki: "rsa2.spki", exception: "Error: EVP_PKEY_decrypt() failed" },
]};

run([rsa_tsuite]);
