var fs = require('fs');
var fsp  = fs.promises;
var dname = './build/test/';
var fname = (d) => d + '/fs_promises_006_file';
var fname_utf8 = (d) => d + '/fs_promises_αβγ_006';

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

Promise.resolve()
.then(() => testSync)
.then(() => {
    console.log('test fs.renameSync');
})
.catch((e) => {
    console.log('test fs.renameSync failed', JSON.stringify(e));
})

.then(testCallback)
.then(() => {
    console.log('test fs.rename');
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
    console.log('test fsp.rename');
})
.catch((e) => {
    console.log('test fsp.rename failed', JSON.stringify(e));
});
