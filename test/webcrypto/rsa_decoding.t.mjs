/*---
includes: [compatFs.js, compatBuffer.js, compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    if (!has_buffer() || !has_webcrypto()) {
        return 'SKIPPED';
    }

    let pem = fs.readFileSync(`test/webcrypto/${params.pem}`);
    let enc = base64decode(fs.readFileSync(`test/webcrypto/${params.src}`));

    let key = await crypto.subtle.importKey("pkcs8", pem_to_der(pem, "PRIVATE"),
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

    tests: [
        { pem: "rsa.pkcs8", src: "text.base64.rsa-oaep.enc", expected: "WAKAWAKA" },
]};

run([rsa_tsuite])
.then($DONE, $DONE);
