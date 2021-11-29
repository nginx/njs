/*---
includes: [compareArray.js, compatFs.js]
flags: [async]
---*/

var fname = 'build/test/fs_promises_02';

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
        resolve(Boolean(failed));
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
                resolve(Boolean(failed));
            });
        });
    });
});

let stages = [];

Promise.resolve()
.then(() => testSync)
.then(failed => {
    stages.push('testSync');
    assert.sameValue(failed, false, 'testSync');
})
.then(() => testCallback)
.then(failed => {
    stages.push('testCallback');
    assert.sameValue(failed, false, 'testCallback');
})
.then(() => {
    fs.writeFileSync(fname, fname);

    return fsp.access(fname)
        .then(() => fsp.access(fname, fs.constants.R_OK | fs.constants.W_OK))
        .then(() => fsp.access(fname + '___'));
})
.then(() => {
    $DONOTEVALUATE();
})
.catch(e => {
    stages.push('testPromise');
    assert.sameValue(e.syscall, 'access', 'testPromise');
    assert.sameValue(e.path, fname + '___', 'testPromise');
    assert.sameValue(e.code, 'ENOENT', 'testPromise');
})
.then(() => {
    assert.compareArray(stages, ["testSync", "testCallback", "testPromise"]);
})
.then($DONE, $DONE);
