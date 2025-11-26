/*---
includes: [compareArray.js, compatFs.js]
flags: [async]
---*/

var dname = `${test_dir}/mkdir_error_path`;

let stages = [];

var testSync = () => new Promise((resolve, reject) => {
    try {
        try { fs.unlinkSync(dname + '/a/b'); } catch (e) {}
        try { fs.rmdirSync(dname + '/a'); } catch (e) {}
        try { fs.rmdirSync(dname); } catch (e) {}

        fs.mkdirSync(dname + '/a', {recursive: true});
        fs.writeFileSync(dname + '/a/b', 'blocking file');

        try {
            fs.mkdirSync(dname + '/a/b/c/d', {recursive: true});
            reject(new Error('Expected ENOTDIR'));
        } catch (e) {
            if (e.code != 'ENOTDIR') {
                reject(e);
            }

            if (!e.path.includes('/c/d')) {
                reject(new Error('Path truncated: ' + e.path));
            }
        }

        fs.unlinkSync(dname + '/a/b');
        fs.rmdirSync(dname + '/a');
        fs.rmdirSync(dname);

        stages.push("mkdirSync");

        resolve();
    } catch (e) {
        reject(e);
    }
});

var testCallback = () => new Promise((resolve, reject) => {
    try {
        try { fs.unlinkSync(dname + '/a/b'); } catch (e) {}
        try { fs.rmdirSync(dname + '/a'); } catch (e) {}
        try { fs.rmdirSync(dname); } catch (e) {}

        fs.mkdir(dname + '/a', {recursive: true}, (err) => {
            if (err) {
                reject(err);
                return;
            }

            fs.writeFile(dname + '/a/b', 'blocking file', (err) => {
                if (err) {
                    reject(err);
                    return;
                }

                fs.mkdir(dname + '/a/b/c/d', {recursive: true}, (err) => {
                    if (!err || err.code != 'ENOTDIR') {
                        reject(new Error('Expected ENOTDIR'));
                        return;
                    }

                    if (!err.path.includes('/c/d')) {
                        reject(new Error('Path truncated: ' + err.path));
                        return;
                    }

                    fs.unlinkSync(dname + '/a/b');
                    fs.rmdirSync(dname + '/a');
                    fs.rmdirSync(dname);

                    stages.push("mkdir");

                    resolve();
                });
            });
        });
    } catch (e) {
        reject(e);
    }
});

let testFsp = () => Promise.resolve()
.then(() => {
    try { fs.unlinkSync(dname + '/a/b'); } catch (e) {}
    try { fs.rmdirSync(dname + '/a'); } catch (e) {}
    try { fs.rmdirSync(dname); } catch (e) {}
})
.then(() => fsp.mkdir(dname + '/a', {recursive: true}))
.then(() => fsp.writeFile(dname + '/a/b', 'blocking file'))
.then(() => fsp.mkdir(dname + '/a/b/c/d', {recursive: true}))
.then(() => {
    throw new Error('Expected ENOTDIR');
})
.catch((e) => {
    if (e.code != 'ENOTDIR') {
        throw e;
    }

    if (!e.path.includes('/c/d')) {
        throw new Error('Path truncated: ' + e.path);
    }
})
.then(() => fsp.unlink(dname + '/a/b'))
.then(() => fsp.rmdir(dname + '/a'))
.then(() => fsp.rmdir(dname))
.then(() => {
    stages.push("fsp.mkdir");
})

let p = Promise.resolve()
    .then(testSync)
    .then(testCallback)
    .then(testFsp)
    .then(() => assert.compareArray(stages, ['mkdirSync', 'mkdir', 'fsp.mkdir']))

p.then($DONE, $DONE);
