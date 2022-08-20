/*---
includes: [compareArray.js, compatFs.js]
flags: [async]
---*/

var fname = `${test_dir}/fs_promises_02`;

let stages = [];

var testSync = () => new Promise((resolve, reject) => {
    try {
        fs.writeFileSync(fname, fname);

        fs.accessSync(fname);
        fs.accessSync(fname, fs.constants.R_OK | fs.constants.W_OK);

        try {
            fs.accessSync(fname + '___');
            reject(new Error('fs.accessSync error 1'));
        } catch (e) {
            if (e.syscall != 'access' || e.code != 'ENOENT') {
                reject(new Error('fs.accessSync error 2'));
            }
        }

        stages.push('testSync');

        resolve();

    } catch (e) {
        reject(e);
    }
});

var testCallback = () => new Promise((resolve, reject) => {
    fs.writeFileSync(fname, fname);

    fs.access(fname, (err) => {
        if (err) {
            reject(new Error('fs.access error 1'));
        }

        fs.access(fname, fs.constants.R_OK | fs.constants.W_OK, (err) => {
            if (err) {
                reject(err);
            }

            fs.access(fname + '___', (err) => {
                if (!err
                    || err.syscall != 'access'
                    || err.code != 'ENOENT')
                    {
                        reject(new Error('fs.access error 2'));
                    }

                stages.push('testCallback');

                resolve();
            });
        });
    });
});

let testFsp = () => Promise.resolve()
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

let p = Promise.resolve()
if (has_fs()) {
    p = p
        .then(testSync)
        .then(testCallback)
        .then(testFsp)
        .then(() => assert.compareArray(stages, ["testSync", "testCallback", "testPromise"]))
}

p.then($DONE, $DONE);
