if (typeof crypto == 'undefined' && typeof require == 'function') {
    globalThis.crypto = require('crypto').webcrypto;
}

function has_webcrypto() {
    return (typeof crypto != 'undefined') ? crypto : null;
}

async function has_webcrypto_algorithm(name, length) {
    if (name !== 'Ed25519' && name !== 'X25519' && name !== 'AES-KW') {
        throw Error('unknown test algorithm: ' + name);
    }

    if (!has_buffer() || !has_webcrypto()) {
        return false;
    }

    try {
        if (name === 'AES-KW') {
            await crypto.subtle.generateKey(
                {name, length: length || 128}, false, ['wrapKey']);

        } else {
            await crypto.subtle.generateKey(name, false,
                name === 'Ed25519' ? ['sign', 'verify'] : ['deriveBits']);
        }

    } catch (e) {
        if (e.message === 'unknown algorithm name: "' + name + '"'
            || (e.name === 'NotSupportedError'
                && e.message === 'Unrecognized algorithm name'))
        {
            return false;
        }

        throw e;
    }

    return true;
}
