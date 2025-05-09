function pem_to_der(pem, type) {
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

function load_jwk(data) {
    if (typeof data == 'string') {
        let json = fs.readFileSync(`test/webcrypto/${data}`);
        return JSON.parse(json);
    }

    return data;
}

function compareUsage(a, b) {
    a.sort();
    b.sort();

    if (b.length !== a.length) {
        return false;
    }

    for (var i = 0; i < a.length; i++) {
        if (b[i] !== a[i]) {
            return false;
        }
    }
    return true;
}
