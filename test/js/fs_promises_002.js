var fs = require('fs');
var fsp  = fs.promises;
var fname = './build/test/fs_promises_002';

var testSync = new Promise((resolve, reject) => {
    var failed = false;
    try {
        fs.writeFileSync(fname, fname);

        fs.accessSync(fname);
        fs.accessSync(fname, fs.constants.R_OK | fs.constants.W_OK);

        try {
            fs.accessSync(fname + '___');
            failed = true;
        } catch(e) {
            failed = (e.syscall != 'access') || e.code != 'ENOENT';
        }
        resolve(failed);
    } catch (e) {
        reject(e);
    }
});

var testCallback = new Promise((resolve, reject) => {
    var failed = false;

    fs.writeFileSync(fname, fname);

    fs.access(fname, (err) => {
        failed = (err !== undefined);
        fs.access(fname, fs.constants.R_OK | fs.constants.W_OK, (err) => {
            failed |= (err !== undefined);
            fs.access(fname + '___', (err) => {
                failed |= ((err === undefined) || (err.syscall != 'access')
                                               || err.code != 'ENOENT');
                resolve(failed);
            });
        });
    });
});

Promise.resolve()
.then(() => testSync)
.then((failed) => {
    console.log('testSync ok', !failed);
})
.catch((e) => {
    console.log('testSync failed', e);
})
.then(() => testCallback)
.then((failed) => {
    console.log('testCallback ok', !failed);
})
.catch((e) => {
    console.log('testCallback failed', e);
})
.then(() => {
    fs.writeFileSync(fname, fname);

    return fsp.access(fname)
        .then(() => fsp.access(fname, fs.constants.R_OK | fs.constants.W_OK))
        .then(() => fsp.access(fname + '___'));
})
.then(() => {
    console.log('testPromise failed');
})
.catch((e) => {
    console.log('testPromise ok', (e.syscall == 'access') && (e.path == fname + '___')
                                                          && e.code == 'ENOENT');
})
;
