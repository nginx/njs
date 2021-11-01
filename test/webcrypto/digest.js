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

function p(args) {
    let params = Object.assign({}, args);
    params.data = Buffer.from(params.data, "hex");
    return params;
}

async function test(params) {
    let digest = await crypto.subtle.digest(params.name, params.data);
    digest = Buffer.from(digest).toString("hex");

    if (params.expected != digest) {
        throw Error(`${params.name} digest failed expected: "${params.expected}" vs "${digest}"`);
    }

    return 'SUCCESS';
}

let digest_tsuite = {
    name: "SHA digest",
    T: test,
    prepare_args: p,
    opts: { },

    tests: [
        { name: "XXX", data: "",
          exception: "TypeError: unknown hash name: \"XXX\"" },
        { name: "SHA-256", data: "",
          expected: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { name: "SHA-256", data: "aabbccdd",
          expected: "8d70d691c822d55638b6e7fd54cd94170c87d19eb1f628b757506ede5688d297" },
        { name: "SHA-256", data: "aabbccdd".repeat(4096),
          expected: "25077ac2e5ba760f015ef34b93bc2b4682b6b48a94d65e21aaf2c8a3a62f6368" },
        { name: "SHA-384", data: "",
          expected: "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b" },
        { name: "SHA-384", data: "aabbccdd",
          expected: "f9616ef3495efbae2f6af1a754620f3034487e9c60f3a9ef8138b5ed55cdd8d18ad9565653a5d68f678bd34cfa6f4490" },
        { name: "SHA-384", data: "aabbccdd".repeat(4096),
          expected: "50502d6e89bc34ecc826e0d56ccba0e010eff7b2b532e3bd627f4c828f6c741bf518fc834559360ccf7770f1b4d655d8" },
        { name: "SHA-512", data: "",
          expected: "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
        { name: "SHA-512", data: "aabbccdd",
          expected: "48e218b30d4ea16305096fe35e84002a0d262eb3853131309423492228980c60238f9eed238285036f22e37c4662e40c80a461000a7aa9a03fb3cb6e4223e83b" },
        { name: "SHA-512", data: "aabbccdd".repeat(4096),
          expected: "9fcd0bd297646e207a2d655feb4ed4473e07ff24560a1e180a5eb2a67824f68affd9c7b5a8f747b9c39201f5f86a0085bb636c6fc34c216d9c10b4d728be096a" },
        { name: "SHA-1", data: "",
          expected: "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
        { name: "SHA-1", data: "aabbccdd",
          expected: "a7b7e9592daa0896db0517bf8ad53e56b1246923" },
        { name: "SHA-1", data: "aabbccdd".repeat(4096),
          expected: "cdea58919606ea9ae078f7595b192b84446f2189" },
]};

run([digest_tsuite]);
