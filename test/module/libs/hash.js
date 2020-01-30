function hash() {
    var h = crypto.createHash('md5');
    var v = h.update('AB').digest('hex');
    return v;
}

import name from 'name.js';
import crypto from 'crypto';

export default {hash, name};
