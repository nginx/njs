var fs = require('fs');
var fsp  = fs.promises;
var dname = './build/test/';
var fname = dname + 'fs_promises_004';
var fname_utf8 = dname + 'fs_promises_αβγ_004';
var lname = dname + 'fs_promises_004_lnk';


var testSync = () => new Promise((resolve, reject) => {
    try {
        try {
            fs.unlinkSync(fname);
        } catch (e) {
            void e;
        }
        try {
            fs.unlinkSync(lname);
        } catch (e) {
            void e;
        }

        try {
            fs.realpathSync(fname);
            throw new Error('fs.realpathSync error 1');
        } catch (e) {
            if (e.syscall != 'realpath' || e.code != 'ENOENT') {
                throw e;
            }
        }

        fs.writeFileSync(fname, fname);
        fs.writeFileSync(fname_utf8, fname_utf8);

        var rname = fs.realpathSync(fname);

        fs.symlinkSync(rname, lname);

        if (fs.realpathSync(lname) != rname) {
            throw new Error('fs.symlinkSync error 2');
        }

        if (fs.readFileSync(lname) != fname) {
            throw new Error('fs.symlinkSync error 3');
        }

        var rname_utf8 = fs.realpathSync(fname_utf8);
        if (rname_utf8.slice(-7,-4) != 'αβγ') {
            throw new Error('fs.realpathSync error 2');
        }

        fs.unlinkSync(lname);
        fs.accessSync(fname);
        fs.unlinkSync(fname);
        fs.unlinkSync(fname_utf8);

        resolve();

    } catch (e) {
        reject(e);
    }
});


var testCallback = () => new Promise((resolve, reject) => {
    try {
        try {
            fs.unlinkSync(fname);
        } catch (e) {
            void e;
        }
        try {
            fs.unlinkSync(lname);
        } catch (e) {
            void e;
        }

        fs.realpath(fname, (err) => {
            if (!err) {
                reject(new Error('fs.realpath error 1'));
                return;
            }
            if (err.syscall != 'realpath' || err.code != 'ENOENT') {
                reject(err);
                return;
            }

            try {
                fs.writeFileSync(fname, fname);
            } catch (e) {
                reject(e);
                return;
            }

            fs.realpath(fname, (err, rname) => {
                if (err) {
                    reject(err);
                    return;
                }

                fs.symlink(rname, lname, (err) => {
                    if (err) {
                        reject(err);
                        return;
                    }

                    fs.realpath(lname, undefined, (err, xname) => {
                        if (err) {
                            reject(err);
                            return;
                        }

                        if (rname != xname) {
                            reject(new Error('fs.symlink error 1'));
                            return;
                        }

                        try {
                            if (fs.readFileSync(lname) != fname) {
                                reject(new Error('fs.symlink error 2'));
                                return;
                            }

                            fs.unlinkSync(lname);
                            fs.accessSync(fname);
                            fs.unlinkSync(fname);

                        } catch (e) {
                            reject(e);
                            return;
                        }

                        resolve();
                    });
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
    console.log('test fs.symlinkSync');
})
.catch((e) => {
    console.log('test fs.symlinkSync failed', e);
})

.then(testCallback)
.then(() => {
    console.log('test fs.symlink');
})
.catch((e) => {
    console.log('test fs.symlink failed', e);
})

.then(() => fsp.unlink(fname)
            .catch(() => {}))
.then(() => fsp.unlink(lname)
            .catch(() => {}))
.then(() => fsp.realpath(fname)
            .then(() => { throw new Error('fsp.realpath error 1') }))
.catch((e) => {
    if (e.syscall != 'realpath' || e.code != 'ENOENT') {
        throw e;
    }
})
.then(() => {
    fs.writeFileSync(fname, fname);

    return fsp.realpath(fname);
})
.then((rname) => fsp.symlink(rname, lname)
                 .then(() => rname))
.then((rname) => fsp.realpath(lname)
                 .then((xname) => {
                     if (rname != xname) {
                        throw new Error('fsp.symlink error 2');
                     }
                 }))
.then(() => {
    if (fs.readFileSync(lname) != fname) {
        throw new Error('fsp.symlink error 3');
    }

    fs.unlinkSync(lname);
    fs.accessSync(fname);
    fs.unlinkSync(fname);
})

.then(() => {
    console.log('test fsp.symlink');
})
.catch((e) => {
    console.log('test fsp.symlink failed', e);
});
