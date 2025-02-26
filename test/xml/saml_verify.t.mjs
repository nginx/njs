/*---
includes: [compatFs.js, compatXml.js, compatWebcrypto.js, compatNjs.js, runTsuite.js]
flags: [async]
---*/

async function verify(params) {
    let file_data = fs.readFileSync(`test/xml/${params.saml}`);
    let key_data = fs.readFileSync(`test/webcrypto/${params.key.file}`);

    if (params.sign) {
        let sign_key_data = fs.readFileSync(`test/webcrypto/${params.key.sign_file}`);
        let signed = await signSAML(xml.parse(file_data), sign_key_data);
        file_data = xml.c14n(signed);
        //console.log((new TextDecoder()).decode(file_data));
    }

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
 * signSAML() signs a SAML message template
 *
 *  The message to sign should already contain a full Signature element
 *  with SignedInfo and Reference elements below.
 *  The only parts that are missing are DigestValue and SignatureValue.
 *   <ds:Signature xmlns:ds="http://www.w3.org/2000/09/xmldsig#">
 *       <ds:SignedInfo>
 *           <ds:CanonicalizationMethod Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#" />
 *           <ds:SignatureMethod Algorithm="http://www.w3.org/2001/04/xmldsig-more#rsa-sha256" />
 *           <ds:Reference URI="#_0x14956c887e664bdb71d7685b89b70619">
 *               <ds:Transforms>
 *                   <ds:Transform Algorithm="http://www.w3.org/2000/09/xmldsig#enveloped-signature" />
 *                   <ds:Transform Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#" />
 *               </ds:Transforms>
 *               <ds:DigestMethod Algorithm="http://www.w3.org/2001/04/xmlenc#sha256" />
 *               <ds:DigestValue></ds:DigestValue>
 *           </ds:Reference>
 *       </ds:SignedInfo>
 *       <ds:SignatureValue></ds:SignatureValue>
 *   </ds:Signature>
 *
 * The following signature algorithms are supported:
 * - http://www.w3.org/2001/04/xmldsig-more#rsa-sha256
 * - http://www.w3.org/2000/09/xmldsig#rsa-sha1
 *
 * The following digest algorithms are supported:
 * - http://www.w3.org/2000/09/xmldsig#sha1
 * - http://www.w3.org/2001/04/xmlenc#sha256
 *
 * As a part of the signing process, the following attributes are set:
 * - xml:ID
 *   The value is a random 16 bytes hex string.
 * - IssueInstant
 *   The value is the current time in ISO 8601 format.
 *
 * @param doc an XMLDoc object returned by xml.parse().
 * @param key_data is PKCS #8 in PEM format.
 */
async function signSAML(saml, key_data) {
    const root = saml.$root;
    const rootSignature = root.Signature;

    const rnd = Buffer.alloc(16);
    crypto.getRandomValues(rnd);
    const id = rnd.toString('hex');

    root.setAttribute('xml:ID', id);
    rootSignature.SignedInfo.Reference.setAttribute('URI', `#${id}`);
    root.setAttribute('IssueInstant', (new Date()).toISOString());

    await digestSAML(rootSignature, true);
    await signatureSAML(rootSignature, key_data, true);
    return saml;
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
        if (!await digestSAML(assertionSignature)) {
            return false;
        }

        if (!await signatureSAML(assertionSignature, key_data)) {
            return false;
        }
    }

    if (rootSignature) {
        if (!await digestSAML(rootSignature)) {
            return false;
        }

        if (!await signatureSAML(rootSignature, key_data)) {
            return false;
        }
    }

    return true;
}

async function digestSAML(signature, produce) {
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

    const c14n = xml.exclusiveC14n(parent, signature, withComments, prefixList);
    const dgst = await crypto.subtle.digest(hash, c14n);
    const b64dgst = Buffer.from(dgst).toString('base64');

    if (produce) {
        signedInfo.Reference.DigestValue.$text = b64dgst;
        return b64dgst;
    }

    const expectedDigest = signedInfo.Reference.DigestValue.$text;

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

async function signatureSAML(signature, key_data, produce) {
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

    const withComments = signedInfo.CanonicalizationMethod
                         .$attr$Algorithm.slice(39) == 'WithComments';

    const signedInfoC14n = xml.exclusiveC14n(signedInfo, null, withComments);

    if (produce) {
        const der = keyPem2Der(key_data, "PRIVATE");
        const key = await crypto.subtle.importKey("pkcs8", der, { name: method, hash },
                                                  false, [ "sign" ]);

        let sig =  await crypto.subtle.sign({ name: method }, key, signedInfoC14n);

        signature.SignatureValue.$text = Buffer.from(sig).toString('base64');
        return signature;
    }

    const der = keyPem2Der(key_data, "PUBLIC");
    const key = await crypto.subtle.importKey("spki", der, { name: method, hash },
                                              false, [ "verify" ]);

    const expectedValue = base64decode(signature.SignatureValue.$text);
    return await crypto.subtle.verify({ name: method }, key, expectedValue,
                                      signedInfoC14n);
}

let saml_verify_tsuite = {
    name: "SAML verify",
    skip: () => (!has_njs() || !has_webcrypto() || !has_xml()),
    T: verify,
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

        { saml: "auth_r.xml", sign: true, key: { sign_file: "rsa.pkcs8" }, expected: true },
]};

run([
    saml_verify_tsuite,
])
.then($DONE, $DONE);
