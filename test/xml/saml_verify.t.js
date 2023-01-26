/*---
includes: [compatFs.js, compatXml.js, compatWebcrypto.js, compatNjs.js, runTsuite.js]
flags: [async]
---*/

async function verify(params) {
    let file_data = fs.readFileSync(`test/xml/${params.saml}`);
    let key_data = fs.readFileSync(`test/webcrypto/${params.key.file}`);
    let saml = xml.parse(file_data);

    let r = await verifySAMLSignature(saml, key_data)
                .catch (e => {
                    if (e.toString().startsWith("Error: EVP_PKEY_CTX_set_signature_md() failed")) {
                        /* Red Hat Enterprise Linux: SHA-1 is disabled */
                        return "SKIPPED";
                    }
                });

    if (r == "SKIPPED") {
        return r;
    }

    if (params.expected !== r) {
        throw Error(`VERIFY ${params.saml} with key ${params.key.file} failed expected: "${params.expected}" vs "${r}"`);
    }

    return 'SUCCESS';
}

/*
 * verifySAMLSignature() implements a verify clause
 * from Profiles for the OASIS SAML V2.0
 * 4.1.4.3 <Response> Message Processing Rules
 *  Verify any signatures present on the assertion(s) or the response
 *
 * verification is done in accordance with
 * Assertions and Protocols for the OASIS SAML V2.0
 * 5.4 XML Signature Profile
 *
 * The following signature algorithms are supported:
 * - http://www.w3.org/2001/04/xmldsig-more#rsa-sha256
 * - http://www.w3.org/2000/09/xmldsig#rsa-sha1
 *
 * The following digest algorithms are supported:
 * - http://www.w3.org/2000/09/xmldsig#sha1
 * - http://www.w3.org/2001/04/xmlenc#sha256
 *
 * @param doc an XMLDoc object returned by xml.parse().
 * @param key_data is SubjectPublicKeyInfo in PEM format.
 */

async function verifySAMLSignature(saml, key_data) {
    const root = saml.$root;
    const rootSignature = root.Signature;

    if (!rootSignature) {
        throw Error(`SAML message is unsigned`);
    }

    const assertion = root.Assertion;
    const assertionSignature = assertion ? assertion.Signature : null;

    if (assertionSignature) {
        if (!await verifyDigest(assertionSignature)) {
            return false;
        }

        if (!await verifySignature(assertionSignature, key_data)) {
            return false;
        }
    }

    if (rootSignature) {
        if (!await verifyDigest(rootSignature)) {
            return false;
        }

        if (!await verifySignature(rootSignature, key_data)) {
            return false;
        }
    }

    return true;
}

async function verifyDigest(signature) {
    const parent = signature.$parent;
    const signedInfo = signature.SignedInfo;
    const reference = signedInfo.Reference;

    /* Sanity check. */

    const URI = reference.$attr$URI;
    const ID = parent.$attr$ID;

    if (URI != `#${ID}`) {
        throw Error(`signed reference URI ${URI} does not point to the parent ${ID}`);
    }

    /*
     * Assertions and Protocols for the OASIS SAML V2.0
     * 5.4.4 Transforms
     *
     * Signatures in SAML messages SHOULD NOT contain transforms other than
     * the http://www.w3.org/2000/09/xmldsig#enveloped-signature and
     * canonicalization transforms http://www.w3.org/2001/10/xml-exc-c14n# or
     * http://www.w3.org/2001/10/xml-exc-c14n#WithComments.
     */

    const transforms = reference.Transforms.$tags$Transform;
    const transformAlgs = transforms.map(t => t.$attr$Algorithm);

    if (transformAlgs[0] != 'http://www.w3.org/2000/09/xmldsig#enveloped-signature') {
        throw Error(`unexpected digest transform ${transforms[0]}`);
    }

    if (!transformAlgs[1].startsWith('http://www.w3.org/2001/10/xml-exc-c14n#')) {
        throw Error(`unexpected digest transform ${transforms[1]}`);
    }

    const namespaces = transformAlgs[1].InclusiveNamespaces;
    const prefixList = namespaces ? namespaces.$attr$PrefixList: null;

    const withComments = transformAlgs[1].slice(39) == 'WithComments';

    let hash;
    const alg = reference.DigestMethod.$attr$Algorithm;

    switch (alg) {
    case "http://www.w3.org/2000/09/xmldsig#sha1":
        hash = "SHA-1";
        break;
    case "http://www.w3.org/2001/04/xmlenc#sha256":
        hash = "SHA-256";
        break;
    default:
        throw Error(`unexpected digest Algorithm ${alg}`);
    }

    const expectedDigest = signedInfo.Reference.DigestValue.$text;

    const c14n = xml.exclusiveC14n(parent, signature, withComments, prefixList);
    const dgst = await crypto.subtle.digest(hash, c14n);
    const b64dgst = Buffer.from(dgst).toString('base64');

    return expectedDigest === b64dgst;
}

function keyPem2Der(pem, type) {
    const pemJoined = pem.toString().split('\n').join('');
    const pemHeader = `-----BEGIN ${type} KEY-----`;
    const pemFooter = `-----END ${type} KEY-----`;
    const pemContents = pemJoined.substring(pemHeader.length, pemJoined.length - pemFooter.length);
    return Buffer.from(pemContents, 'base64');
}

function base64decode(b64) {
    const joined = b64.toString().split('\n').join('');
    return Buffer.from(joined, 'base64');
}

async function verifySignature(signature, key_data) {
    const der = keyPem2Der(key_data, "PUBLIC");

    let method, hash;
    const signedInfo = signature.SignedInfo;
    const alg = signedInfo.SignatureMethod.$attr$Algorithm;

    switch (alg) {
    case "http://www.w3.org/2000/09/xmldsig#rsa-sha1":
        method = "RSASSA-PKCS1-v1_5";
        hash = "SHA-1";
        break;
    case "http://www.w3.org/2001/04/xmldsig-more#rsa-sha256":
        method = "RSASSA-PKCS1-v1_5";
        hash = "SHA-256";
        break;
    default:
        throw Error(`unexpected signature Algorithm ${alg}`);
    }

    const expectedValue = base64decode(signature.SignatureValue.$text);
    const withComments = signedInfo.CanonicalizationMethod
                         .$attr$Algorithm.slice(39) == 'WithComments';

    const signedInfoC14n = xml.exclusiveC14n(signedInfo, null, withComments);

    const key = await crypto.subtle.importKey("spki", der, { name: method, hash },
                                            false, [ "verify" ]);

    return await crypto.subtle.verify({ name: method }, key, expectedValue,
                                      signedInfoC14n);
}

function p(args, default_opts) {
    let params = merge({}, default_opts);
    params = merge(params, args);

    return params;
}

let saml_verify_tsuite = {
    name: "SAML verify",
    skip: () => (!has_njs() || !has_fs() || !has_webcrypto()),
    T: verify,
    prepare_args: p,
    opts: {
        key: { fmt: "spki", file: "rsa.spki" },
    },

    tests: [
        { saml: "auth_r_signed.xml", expected: true },
        { saml: "auth_r_with_comments_signed.xml", expected: true },
        { saml: "auth_r_prefix_list_signed.xml", expected: true },
        { saml: "auth_r_signed2.xml", expected: false },
        { saml: "auth_r_signed.xml", key: { file: "rsa2.spki"}, expected: false },
        { saml: "auth_r_signed2.xml", key: { file: "rsa2.spki"}, expected: true },
        { saml: "response_signed.xml", expected: true },
        { saml: "response_signed_broken.xml", expected: false },
        { saml: "response_signed_broken2.xml", expected: false },
        { saml: "response_signed.xml", key: { file: "rsa2.spki"}, expected: false },
        { saml: "response_assertion_and_message_signed.xml", expected: true },
]};

run([
    saml_verify_tsuite,
])
.then($DONE, $DONE);
