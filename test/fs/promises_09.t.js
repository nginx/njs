/*---
includes: [compareArray.js, compatFs.js]
flags: [async]
---*/

var root = test_dir;
var dname = 'fs_promises_αβγ_09/';
var lname = 'fs_promises_αβγ_09_lnk';
var path = 'one/two/three/αβγ';

var setContent = (root, path) => {
    fs.mkdirSync(root + path, { recursive: true });
    path
        .split('/')
        .forEach((x, i, a) => {
            for (var j = 1; j < 10; ++j) {
                var path = root + a.slice(0, i + 1).join('/') + '_file' + j;
                fs.writeFileSync(path, path);
            }
        });
};


var isNode = () => process.argv[0].includes('node');

let stages = [];

var testSync = () => new Promise((resolve, reject) => {
    try {
        fs.unlinkSync(root + lname);
    } catch (e) {
    }
    try {
        fs.rmdirSync(root + dname, { recursive: true });
    } catch (e) {
    }

    try {

        fs.mkdirSync(root + dname);
        fs.symlinkSync(dname, root + lname);
        try {
            fs.rmdirSync(root + lname);
            throw new Error('fs.rmdirSync() - error 0');
        } catch (e) {
            if (e.code != "ENOTDIR") {
                throw e;
            }
        }
        fs.rmdirSync(root + dname);
        fs.unlinkSync(root + lname);

        if (!isNode()) {
            fs.mkdirSync(root + dname);
            fs.symlinkSync(dname, root + lname);
            try {
                fs.rmdirSync(root + lname, { recursive: true });
                throw new Error('fs.rmdirSync() - error 1');
            } catch (e) {
                if (e.code != "ENOTDIR") {
                    throw e;
                }
            }
            fs.rmdirSync(root + dname);
            fs.unlinkSync(root + lname);
        }

        fs.mkdirSync(root + dname, { mode: 0 });
        fs.rmdirSync(root + dname, { recursive: true });

        setContent(root + dname, path);
        fs.rmdirSync(root + dname, { recursive: true });

        try {
            fs.accessSync(root + dname);
            throw new Error('fs.rmdirSync() - error 2');
        } catch (e) {
            if (e.code != "ENOENT") {
                throw e;
            }
        }

        if (!isNode()) {
            try {
                fs.rmdirSync(root + dname, { recursive: true });
                throw new Error('fs.rmdirSync() - error 3');
            } catch (e) {
                if (e.code != "ENOENT") {
                    throw e;
                }
            }
        }

        stages.push("rmdirSync");

        resolve();

    } catch (e) {
        reject(e);
    }
});

let p = Promise.resolve()
if (has_fs() && has_fs_symbolic_link()) {
    p = p
        .then(testSync)
        .then(() => assert.compareArray(stages, ['rmdirSync']))
}

p.then($DONE, $DONE);
