/*---
includes: [compatWebcrypto.js, runTsuite.js, webCryptoUtils.js]
flags: [async]
---*/

async function test(params) {
    let keyPair;
    
    /* Handle invalid keyPair test case */
    if (params.invalidKeyPair) {
        keyPair = { invalidKey: true };
    } else if (params.algorithm === "RSA") {
        keyPair = await crypto.subtle.generateKey(
            {
                name: "RSASSA-PKCS1-v1_5",
                hash: "SHA-256",
                modulusLength: 2048,
                publicExponent: new Uint8Array([1, 0, 1])
            },
            false,
            ["sign", "verify"]
        );
    } else if (params.algorithm === "ECDSA") {
        keyPair = await crypto.subtle.generateKey(
            {
                name: "ECDSA",
                namedCurve: "P-256"
            },
            false,
            ["sign", "verify"]
        );
    }
    
    const cert = await crypto.subtle.generateCertificate(params.options,
                                                         keyPair);
    
    if (!(cert instanceof ArrayBuffer)) {
        throw new Error("Certificate should be an ArrayBuffer");
    }
    
    if (cert.byteLength === 0) {
        throw new Error("Certificate should not be empty");
    }
    
    /* Basic DER format validation - should start with SEQUENCE (0x30) */
    const certBytes = new Uint8Array(cert);
    if (certBytes[0] !== 0x30) {
        throw new Error("Certificate should start with DER SEQUENCE tag");
    }
    
    return 'SUCCESS';
}

let cert_tsuite = {
    name: "Certificate generation",
    skip: () => (!has_webcrypto()),
    T: test,
    prepare_args: (args) => args,

    tests: [
        {
            algorithm: "RSA",
            options: {
                subject: {
                    CN: "Test Certificate"
                },
                serialNumber: "01"
            }
        },
        {
            algorithm: "ECDSA",
            options: {
                subject: {
                    CN: "ECDSA Test Certificate"
                },
                issuer: {
                    CN: "Test CA"
                },
                serialNumber: "02"
            }
        },
        {
            algorithm: "RSA",
            options: {
                subject: {
                    CN: "Minimal Test"
                }
            }
        },
        {
            algorithm: "RSA",
            options: {
                subject: {
                    CN: "Validity Test"
                },
                serialNumber: "03",
                notBefore: 0,
                notAfter: 365 * 24 * 60 * 60 * 1000
            }
        },
        {
            algorithm: "ECDSA",
            options: {
                subject: {
                    CN: "ECDSA Minimal Test"
                },
                serialNumber: "04"
            }
        },
        /* Error cases - missing subject */
        {
            algorithm: "RSA",
            options: {
                serialNumber: "05"
            },
            exception: "TypeError: certificate subject is required"
        },
        /* Error cases - empty subject object */
        {
            algorithm: "RSA",
            options: {
                subject: {},
                serialNumber: "06"
            },
            exception: "Error: X509_NAME_add_entry_by_txt() failed"
        },
        /* Error cases - invalid keyPair */
        {
            invalidKeyPair: true,
            options: {
                subject: {
                    CN: "Test"
                },
                serialNumber: "11"
            },
            exception: "TypeError: keyPair.privateKey is required"
        },
    ]
};

run([cert_tsuite])
.then($DONE, $DONE);
