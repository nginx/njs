if (typeof crypto == 'undefined' && typeof require == 'function') {
    globalThis.crypto = require('crypto').webcrypto;
}

function has_webcrypto() {
    return (typeof crypto != 'undefied') ? crypto : null;
}

