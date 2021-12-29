let fs = null;
let fsp = null;

if (typeof require == 'function') {
    fs = require('fs');
    fsp = fs.promises;
}

function has_fs() {
    return fs;
}

let test_dir = process.env && process.env['NJS_TEST_DIR'] || 'build';
test_dir = `${test_dir}/test`;
