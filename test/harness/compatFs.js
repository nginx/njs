let fs = null;
let fsp = null;

if (typeof require == 'function') {
    fs = require('fs');
    fsp = fs.promises;
}

if (typeof process == 'undefined') {
    globalThis.process = {};
}

let test_dir = process.env && process.env['NJS_TEST_DIR'] || 'build';
test_dir = `${test_dir}/test`;

function has_fs() {
    return fs;
}

function has_fs_symbolic_link() {
    if (!fs) {
        return false;
    }

    let fname = test_dir + '/a';
    let lname = test_dir + '/b';

    try { fs.unlinkSync(fname); fs.unlinkSync(lname); } catch (e) {}

    fs.writeFileSync(fname, fname);

    fname = fs.realpathSync(fname);

    try {
        fs.symlinkSync(fname, lname);
    } catch (e) {
        return false;
    }

    return true;
}

