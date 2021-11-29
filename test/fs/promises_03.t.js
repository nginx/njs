/*---
includes: [compareArray.js, compatFs.js]
flags: [async]
---*/

var fname = 'build/test/fs_promises_03';

var testSync = () => new Promise((resolve, reject) => {
    try {
        try {
            fs.unlinkSync(fname);
        } catch (e) {
            void e;
        }

        try {
            fs.unlinkSync(fname);
            throw new Error('unlinkSync error 1');
        } catch (e) {
            if (e.syscall != 'unlink') {
                throw e;
            }
        }

        fs.writeFileSync(fname, fname);
        fs.unlinkSync(fname);
        try {
            fs.accessSync(fname);
            reject(new Error('unlinkSync error 2'));
            return;
        } catch (e) {
            void e;
        }

        resolve();
    } catch (e) {
        reject(e);
    }
});


var testCallback = () => new Promise((resolve, reject) => {
    fs.unlink(fname, () => {
        fs.unlink(fname, (err) => {
            if (!err) {
                reject(new Error('fs.unlink error 1'));
                return;
            }
            if (err.syscall != 'unlink') {
                reject(err);
                return;
            }

            fs.writeFileSync(fname, fname);
            fs.unlink(fname, (err) => {
                if (err) {
                    reject(err);
                    return;
                }
                try {
                    fs.accessSync(fname);
                    reject(new Error('fs.unlink error 2'));
                    return;
                } catch (e) {
                    void e;
                }
                resolve();
            });
        });
    });
});

let stages = [];

Promise.resolve()
.then(testSync)
.then(() => {
    stages.push("unlinkSync");
})

.then(testCallback)
.then(() => {
    stages.push("unlink");
})

.then(() => fsp.unlink(fname)
            .catch(() => {}))
.then(() => fsp.unlink(fname))
            .then(() => { throw new Error('fsp.unlink error 1'); })
.catch((e) => { if (e.syscall != 'unlink') { throw e; } })

.then(() => {
    fs.writeFileSync(fname, fname);
    return fsp.unlink(fname);
})
.then(() => fsp.access(fname))
            .then(() => { throw new Error('fsp.unlink error 2'); })
.catch((e) => { if (e.syscall != 'access') { throw e; } })

.then(() => {
    stages.push("fsp.unlink");
})
.then(() => assert.compareArray(stages, ['unlinkSync', 'unlink', 'fsp.unlink']))
.then($DONE, $DONE);
