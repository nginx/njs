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
                                            {name:"RSA-OAEP", hash:params.hash},
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

    opts: {
        pem: "rsa.pkcs8",
        hash: "SHA-1",
        expected: "WAKAWAKA",
    },

    tests: [
        { src: "text.base64.rsa-oaep.enc" },
        { src: "text.base64.rsa-oaep-sha256.enc", hash: "SHA-256" },
        { src: "text.base64.rsa-oaep-sha384.enc", hash: "SHA-384" },
]};

run([rsa_tsuite])
.then($DONE, $DONE);
