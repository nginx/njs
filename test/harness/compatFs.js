import fs from 'fs';
let fsp = fs.promises;

if (typeof process == 'undefined') {
    globalThis.process = {};
}

let test_dir = process.env && process.env['NJS_TEST_DIR'] || 'build';
test_dir = `${test_dir}/test`;

function has_fs_symbolic_link() {

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

