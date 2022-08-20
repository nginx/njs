/*---
includes: [compareArray.js, compatFs.js]
flags: [async]
---*/

var dname = `${test_dir}/`;
var fname = (d) => d + '/fs_promises_06_file';
var fname_utf8 = (d) => d + '/fs_promises_αβγ_06';

let stages = [];

var testSync = () => new Promise((resolve, reject) => {
    try {
        try { fs.unlinkSync(fname(dname)); } catch (e) {}
        try { fs.unlinkSync(fname_utf8(dname)); } catch (e) {}

        fs.writeFileSync(fname(dname), fname(dname));

        fs.renameSync(fname(dname), fname_utf8(dname));

        fs.accessSync(fname_utf8(dname));

        try {
            fs.renameSync(fname_utf8(dname), dname);

        } catch (e) {
            if (e.syscall != 'rename'
                || (e.code != 'ENOTDIR' && e.code != 'EISDIR'))
            {
                reject(new Error('fs.unlinkSync error 1'));
            }
        }

        stages.push("renameSync");

        resolve();

    } catch (e) {
        reject(e);
    }
});

var testCallback = () => new Promise((resolve, reject) => {
    try {
        try { fs.unlinkSync(fname(dname)); } catch (e) {}
        try { fs.unlinkSync(fname_utf8(dname)); } catch (e) {}

        fs.writeFileSync(fname(dname), fname(dname));

        fs.rename(fname(dname), fname_utf8(dname), err => {
            if (err) {
                reject(new Error('fs.unlink error 1'));
            }

            fs.accessSync(fname_utf8(dname));

            fs.rename(fname_utf8(dname), dname, err => {
                if (err.syscall != 'rename'
                    || (err.code != 'ENOTDIR' && err.code != 'EISDIR'))
                {
                    reject(new Error('fs.unlink error 2'));
                }
            });

            stages.push("rename");

            resolve();
        });

    } catch (e) {
        reject(e);
    }
});

let testFsp = () => Promise.resolve()
.then(() => {
    try { fs.unlinkSync(fname(dname)); } catch (e) {}
    try { fs.unlinkSync(fname_utf8(dname)); } catch (e) {}

    fs.writeFileSync(fname(dname), fname(dname));
})
.then(() => fsp.rename(fname(dname), fname_utf8(dname)))
.then(() => fsp.access(fname_utf8(dname)))
.then(() => fsp.rename(fname_utf8(dname), dname))
.catch(e => {
    if (e.syscall != 'rename'
        || (e.code != 'ENOTDIR' && e.code != 'EISDIR'))
    {
        throw new Error('fsp.rename error 1');
    }
})
.then(() => {
    stages.push("fsp.rename");
})

let p = Promise.resolve()
if (has_fs()) {
    p = p
        .then(testSync)
        .then(testCallback)
        .then(testFsp)
        .then(() => assert.compareArray(stages, ["renameSync", "rename", "fsp.rename"]))
}

p.then($DONE, $DONE);
