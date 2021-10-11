const fs = require('fs');

if (typeof crypto == 'undefined') {
    crypto = require('crypto').webcrypto;
}

function parse_options(argv) {
    let opts = JSON.parse(argv[2] ? argv[2] : "{}");

    if (!opts.key) {
        opts.key = Buffer.from("00112233001122330011223300112233", "hex");

    } else {
        opts.key = Buffer.from(opts.key, "hex");
    }

    if (!opts.iv) {
        opts.iv = Buffer.from("44556677445566774455667744556677", "hex");

    } else {
        opts.iv = Buffer.from(opts.iv, "hex");
    }

    if (opts.additionalData) {
        opts.additionalData = Buffer.from(opts.additionalData, "hex");
    }

    if (!opts['in']) {
        throw Error("opts.in is expected");
    }

    return opts;
}

(async function main() {
    let opts = parse_options(process.argv);
    let stdin = fs.readFileSync(`test/webcrypto/${opts['in']}`);
    let key = await crypto.subtle.importKey("raw", opts.key,
                                            {name: "AES-GCM"},
                                            false, ["encrypt"]);

    let params = Object.assign(opts);
    params.name = "AES-GCM";

    let enc = await crypto.subtle.encrypt(params, key, stdin);

    console.log(Buffer.from(enc).toString("base64"));
})()
.catch(e => {
    console.log(`exception:${e.stack}`);
})
