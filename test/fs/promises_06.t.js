/*---
includes: [compareArray.js, compatFs.js]
flags: [async]
---*/

var dname = 'build/test/';
var fname = (d) => d + '/fs_promises_06_file';
var fname_utf8 = (d) => d + '/fs_promises_αβγ_06';

var testSync = new Promise((resolve, reject) => {
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
                throw e;
            }
        }

        resolve();

    } catch (e) {
        reject(e);
    }
});

var testCallback = new Promise((resolve, reject) => {
    try {
        try { fs.unlinkSync(fname(dname)); } catch (e) {}
        try { fs.unlinkSync(fname_utf8(dname)); } catch (e) {}

        fs.writeFileSync(fname(dname), fname(dname));

        fs.rename(fname(dname), fname_utf8(dname), err => {
            if (err) {
                throw err;
            }
        });

        fs.accessSync(fname_utf8(dname));

        fs.rename(fname_utf8(dname), dname, err => {
            if (err.syscall != 'rename'
                || (err.code != 'ENOTDIR' && err.code != 'EISDIR'))
            {
                throw err;
            }
        });

        resolve();

    } catch (e) {
        reject(e);
    }
});

let stages = [];

Promise.resolve()
.then(() => testSync)
.then(() => {
    stages.push("renameSync");
})

.then(testCallback)
.then(() => {
    stages.push("rename");
})
.catch((e) => {
    console.log('test fs.rename failed', JSON.stringify(e));
})

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
        throw e;
    }
})
.then(() => {
    stages.push("fsp.rename");
})
.then(() => assert.compareArray(stages, ["renameSync", "rename", "fsp.rename"]))
.then($DONE, $DONE);
