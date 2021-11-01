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

function merge(to, from) {
    let r = Object.assign({}, to);
    Object.keys(from).forEach(v => {
        if (typeof r[v] == 'object' && typeof from[v] == 'object') {
            r[v] = merge(r[v], from[v]);

        } else if (typeof from[v] == 'object') {
            r[v] = Object.assign({}, from[v]);

        } else {
            r[v] = from[v];
        }
    })

    return r;
};

function base64decode(b64) {
    const joined = b64.toString().split('\n').join('');
    return Buffer.from(joined, 'base64');
}

function pem_to_der(pem, type) {
    const pemJoined = pem.toString().split('\n').join('');
    const pemHeader = `-----BEGIN ${type} KEY-----`;
    const pemFooter = `-----END ${type} KEY-----`;
    const pemContents = pemJoined.substring(pemHeader.length, pemJoined.length - pemFooter.length);
    return Buffer.from(pemContents, 'base64');
}

function p(args, default_opts) {
    let encoder = new TextEncoder();
    let params = merge({}, default_opts);
    params = merge(params, args);

    switch (params.key.fmt) {
    case "spki":
        let pem = fs.readFileSync(`test/webcrypto/${params.key.file}`);
        params.key.file = pem_to_der(pem, "PUBLIC");
        break;
    case "raw":
        params.key.file = Buffer.from(params.key.file, "hex");
        break;
    }

    params.signature = base64decode(fs.readFileSync(`test/webcrypto/${params.signature}`));
    params.text = encoder.encode(params.text);

    return params;
}


async function test(params) {
    let key = await crypto.subtle.importKey(params.key.fmt,
                                            params.key.file,
                                            params.import_alg,
                                            false, ["verify"]);

    let r = await crypto.subtle.verify(params.verify_alg,
                                       key, params.signature,
                                       params.text);

    if (params.expected !== r) {
        throw Error(`${params.import_alg.name} failed expected: "${params.expected}" vs "${r}"`);
    }

    return 'SUCCESS';
}

let hmac_tsuite = {
    name: "HMAC verify",
    T: test,
    prepare_args: p,
    opts: {
        text: "SigneD-TExt",
        key: { fmt: "raw", file: "aabbcc" },
        import_alg: {
            name: "HMAC",
            hash: "SHA-256",
        },
        verify_alg: {
            name: "HMAC",
        },
    },

    tests: [
        { signature: "text.base64.sha256.hmac.sig", expected: true },
        { signature: "text.base64.sha256.hmac.sig.broken", expected: false },
        { import_alg: { hash: "SHA-1" }, signature: "text.base64.sha1.hmac.sig", expected: true },
        { import_alg: { hash: "SHA-1" }, signature: "text.base64.sha256.hmac.sig", expected: false },
        { key: { file: "aabbccdd" }, signature: "text.base64.sha256.hmac.sig", expected: false },
]};

let rsassa_pkcs1_v1_5_tsuite = {
    name: "RSASSA-PKCS1-v1_5 verify",
    T: test,
    prepare_args: p,
    opts: {
        text: "SigneD-TExt",
        key: { fmt: "spki", file: "rsa.spki" },
        import_alg: {
            name: "RSASSA-PKCS1-v1_5",
            hash: "SHA-256",
        },
        verify_alg: {
            name: "RSASSA-PKCS1-v1_5",
        },
    },

    tests: [
        { signature: "text.base64.sha256.pkcs1.sig", expected: true },
        { text: "SigneD-TExt2", signature: "text.base64.sha256.pkcs1.sig", expected: false },
        { signature: "text.base64.sha1.pkcs1.sig", expected: false },
        { import_alg: { hash: "SHA-1" }, signature: "text.base64.sha1.pkcs1.sig", expected: true },
        { key: { file: "rsa2.spki"}, signature: "text.base64.sha256.pkcs1.sig", expected: false },
]};

let rsa_pss_tsuite = {
    name: "RSA-PSS verify",
    T: test,
    prepare_args: p,
    opts: {
        text: "SigneD-TExt",
        key: { fmt: "spki", file: "rsa.spki" },
        import_alg: {
            name: "RSA-PSS",
            hash: "SHA-256",
        },
        verify_alg: {
            name: "RSA-PSS",
            saltLength: 32,
        },
    },

    tests: [
        { signature: "text.base64.sha256.rsa-pss.32.sig", expected: true },
        { text: "SigneD-TExt2", signature: "text.base64.sha256.rsa-pss.32.sig", expected: false },
        { key: { file: "rsa2.spki"}, signature: "text.base64.sha256.rsa-pss.32.sig", expected: false },
        { verify_alg: { saltLength: 0 }, signature: "text.base64.sha256.rsa-pss.0.sig", expected: true },
        { verify_alg: { saltLength: 0 }, signature: "text.base64.sha256.rsa-pss.0.sig", expected: true },
        { import_alg: { hash: "SHA-1" }, signature: "text.base64.sha256.rsa-pss.32.sig", expected: false },
        { import_alg: { hash: "SHA-1" }, verify_alg: { saltLength: 16 }, signature: "text.base64.sha1.rsa-pss.16.sig",
          expected: true },
        { verify_alg: { saltLength: 16 }, signature: "text.base64.sha256.rsa-pss.32.sig", expected: false },
]};

let ecdsa_tsuite = {
    name: "ECDSA verify",
    T: test,
    prepare_args: p,
    opts: {
        text: "SigneD-TExt",
        key: { fmt: "spki", file: "ec.spki" },
        import_alg: {
            name: "ECDSA",
            namedCurve: "P-256",
        },
        verify_alg: {
            name: "ECDSA",
            hash: "SHA-256",
        },
    },

    tests: [
        { signature: "text.base64.sha256.ecdsa.sig", expected: true },
        { signature: "text.base64.sha1.ecdsa.sig", expected: false },
        { verify_alg: { hash: "SHA-1"}, signature: "text.base64.sha1.ecdsa.sig", expected: true },
        { key: { file: "ec2.spki" }, signature: "text.base64.sha256.ecdsa.sig", expected: false },
]};

run([
    hmac_tsuite,
    rsassa_pkcs1_v1_5_tsuite,
    rsa_pss_tsuite,
    ecdsa_tsuite,
]);
