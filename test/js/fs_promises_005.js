var fs = require('fs');
var fsp  = fs.promises;
var rname = './build/test/';
var dname = rname + 'fs_promises_005';
var dname_utf8 = rname + 'fs_promises_αβγ_005';
var fname = (d) => d + '/fs_promises_005_file';


var testSync = () => new Promise((resolve, reject) => {
    try {
        try { fs.unlinkSync(fname(dname)); } catch (e) {}
        try { fs.unlinkSync(fname(dname_utf8)); } catch (e) {}
        try { fs.rmdirSync(dname); } catch (e) {}
        try { fs.rmdirSync(dname_utf8); } catch (e) {}

        fs.mkdirSync(dname);

        try {
            fs.mkdirSync(dname);

        } catch (e) {
            if (e.syscall != 'mkdir' || e.code != 'EEXIST') {
                throw e;
            }
        }

        fs.writeFileSync(fname(dname), fname(dname));

        try {
            fs.rmdirSync(dname);

        } catch (e) {
            if (e.syscall != 'rmdir'
                || (e.code != 'ENOTEMPTY' && e.code != 'EEXIST'))
            {
                throw e;
            }
        }

        fs.unlinkSync(fname(dname));

        fs.rmdirSync(dname);

        fs.mkdirSync(dname_utf8, 0o555);

        try {
            fs.writeFileSync(fname(dname_utf8), fname(dname_utf8));

        } catch (e) {
            if (e.syscall != 'open' || e.code != 'EACCES') {
                throw e;
            }
        }

        try {
            fs.unlinkSync(dname_utf8);

        } catch (e) {
            if (e.syscall != 'unlink' || (e.code != 'EISDIR' && e.code != 'EPERM')) {
                throw e;
            }
        }

        fs.rmdirSync(dname_utf8);

        resolve();

    } catch (e) {
        reject(e);
    }
});


var testCallback = () => new Promise((resolve, reject) => {
    try {
        try { fs.unlinkSync(fname(dname)); } catch (e) {}
        try { fs.unlinkSync(fname(dname_utf8)); } catch (e) {}
        try { fs.rmdirSync(dname); } catch (e) {}
        try { fs.rmdirSync(dname_utf8); } catch (e) {}

        fs.mkdir(dname, (err) => {
            if (err) {
                reject(err);
            }

            fs.mkdir(dname, (err) => {
                if (!err || err.code != 'EEXIST') {
                    reject(new Error('fs.mkdir error 1'));
                }

                fs.rmdir(dname, (err) => {
                    if (err) {
                        reject(err);
                    }

                    resolve();
                });
            });
        });

    } catch (e) {
        reject(e);
    }
});


Promise.resolve()
.then(testSync)
.then(() => {
    console.log('test fs.mkdirSync');
})
.catch((e) => {
    console.log('test fs.mkdirSync failed', JSON.stringify(e));
})

.then(testCallback)
.then(() => {
    console.log('test fs.mkdir');
})
.catch((e) => {
    console.log('test fs.mkdir failed', JSON.stringify(e));
})

.then(() => {
    try { fs.unlinkSync(fname(dname)); } catch (e) {}
    try { fs.unlinkSync(fname(dname_utf8)); } catch (e) {}
    try { fs.rmdirSync(dname); } catch (e) {}
    try { fs.rmdirSync(dname_utf8); } catch (e) {}
})
.then(() => fsp.mkdir(dname))
.then(() => fsp.mkdir(dname))
.catch((e) => {
    if (e.syscall != 'mkdir' || e.code != 'EEXIST') {
        throw e;
    }
})
.then(() => fsp.rmdir(dname))
.then(() => {
    console.log('test fsp.mkdir');
})
.catch((e) => {
    console.log('test fsp.mkdir failed', JSON.stringify(e));
});
