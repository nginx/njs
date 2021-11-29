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

