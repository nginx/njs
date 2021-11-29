let fs = null;
let fsp = null;

if (typeof require == 'function') {
    fs = require('fs');
    fsp = fs.promises;
}

function has_fs() {
    return fs;
}
