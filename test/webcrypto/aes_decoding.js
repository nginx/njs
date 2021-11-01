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

function base64decode(b64) {
    const joined = b64.toString().split('\n').join('');
    return Buffer.from(joined, 'base64');
}

function p(args, default_opts) {
    let params = Object.assign({}, default_opts, args);

    params.key = Buffer.from(params.key, "hex");
    params.iv = Buffer.from(params.iv, "hex");
    params.counter = Buffer.from(params.counter, "hex");

    switch (params.name) {
    case "AES-GCM":
        if (params.additionalData) {
            params.additionalData = Buffer.from(params.additionalData, "hex");
        }

        break;
    }

    return params;
}

async function test(params) {
    let enc = base64decode(fs.readFileSync(`test/webcrypto/${params.file}`));
    let key = await crypto.subtle.importKey("raw", params.key,
                                            {name: params.name},
                                            false, ["decrypt"]);

    let plaintext = await crypto.subtle.decrypt(params, key, enc);
    plaintext = new TextDecoder().decode(plaintext);

    if (params.expected != plaintext) {
        throw Error(`${params.name} decoding failed expected: "${params.expected}" vs "${plaintext}"`);
    }

    return 'SUCCESS';
}

let aes_tsuite = {
    name: "AES decoding",
    T: test,
    prepare_args: p,
    opts: {
        key: "00112233001122330011223300112233",
        iv: "44556677445566774455667744556677",
        counter: "44556677445566774455667744556677",
        length: 64
    },

    tests: [
        { name: "AES-GCM", file: "text.base64.aes-gcm128.enc",
          expected: "AES-GCM-SECRET-TEXT" },
        { name: "AES-GCM", file: "text.base64.aes-gcm128-96.enc",
          exception: "Error: EVP_DecryptFinal_ex() failed" },
        { name: "AES-GCM", file: "text.base64.aes-gcm128-96.enc", tagLength: 96,
          expected: "AES-GCM-96-TAG-LENGTH-SECRET-TEXT" },
        { name: "AES-GCM", file: "text.base64.aes-gcm128-extra.enc", additionalData: "deadbeef",
          expected: "AES-GCM-ADDITIONAL-DATA-SECRET-TEXT" },
        { name: "AES-GCM", file: "text.base64.aes-gcm256.enc",
          key: "0011223300112233001122330011223300112233001122330011223300112233",
          expected: "AES-GCM-256-SECRET-TEXT" },
        { name: "AES-GCM", file: "text.base64.aes-gcm256.enc",
          key: "00112233001122330011223300112233001122330011223300112233001122",
          exception: "TypeError: AES-GCM Invalid key length" },
        { name: "AES-CTR", file: "text.base64.aes-ctr128.enc",
          expected: "AES-CTR-SECRET-TEXT" },
        { name: "AES-CTR", file: "text.base64.aes-ctr256.enc",
          key: "0011223300112233001122330011223300112233001122330011223300112233",
          expected: "AES-CTR-256-SECRET-TEXT" },
        { name: "AES-CBC", file: "text.base64.aes-cbc128.enc",
          expected: "AES-CBC-SECRET-TEXT" },
        { name: "AES-CBC", file: "text.base64.aes-cbc256.enc",
          key: "0011223300112233001122330011223300112233001122330011223300112233",
          expected: "AES-CBC-256-SECRET-TEXT" },
]};

run([aes_tsuite]);
