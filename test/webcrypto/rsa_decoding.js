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

function pem_to_der(pem) {
    const pemJoined = pem.toString().split('\n').join('');
    const pemHeader = '-----BEGIN PRIVATE KEY-----';
    const pemFooter = '-----END PRIVATE KEY-----';
    const pemContents = pemJoined.substring(pemHeader.length, pemJoined.length - pemFooter.length);
    return Buffer.from(pemContents, 'base64');
}

function base64decode(b64) {
    const joined = b64.toString().split('\n').join('');
    return Buffer.from(joined, 'base64');
}

async function test(params) {
    let pem = fs.readFileSync(`test/webcrypto/${params.pem}`);
    let enc = base64decode(fs.readFileSync(`test/webcrypto/${params.src}`));

    let key = await crypto.subtle.importKey("pkcs8", pem_to_der(pem),
                                            {name:"RSA-OAEP", hash:"SHA-1"},
                                            false, ["decrypt"]);

    let plaintext = await crypto.subtle.decrypt({name: "RSA-OAEP"}, key, enc);
    plaintext = new TextDecoder().decode(plaintext);

    if (params.expected != plaintext) {
        throw Error(`RSA-OAEP decoding failed expected: "${params.expected}" vs "${plaintext}"`);
    }

    return "SUCCESS";
}

let rsa_tsuite = {
    name: "RSA-OAEP decoding",
    T: test,
    prepare_args: (v) => v,
    opts: { },

    tests: [
        { pem: "rsa.pkcs8", src: "text.base64.rsa-oaep.enc", expected: "WAKAWAKA" },
        { pem: "ec.pkcs8", src: "text.base64.rsa-oaep.enc", exception: "Error: RSA key is not found" },
        { pem: "rsa.pkcs8.broken", src: "text.base64.rsa-oaep.enc", exception: "Error: d2i_PKCS8_PRIV_KEY_INFO_bio() failed" },
]};

run([rsa_tsuite]);
