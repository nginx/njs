function hash() {
    return sub.hash();
}

function exception() {
    return sub.error();
}

import sub from 'sub/sub1.js';

export default {hash, exception};
