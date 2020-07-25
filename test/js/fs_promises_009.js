var fs = require('fs');
var fsp  = fs.promises;
var root = './build/test/';
var dname = 'fs_promises_αβγ_009/';
var lname = 'fs_promises_αβγ_009_lnk';
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

        resolve();

    } catch (e) {
        reject(e);
    }
});


Promise.resolve()
.then(testSync)
.then(() => {
    console.log('test recursive fs.rmdirSync()');
})
.catch((e) => {
    console.log('test failed recursive fs.rmdirSync()', e.message, JSON.stringify(e));
});
