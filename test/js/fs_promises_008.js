var fs = require('fs');
var fsp  = fs.promises;
var dname = './build/test/fs_promises_αβγ_008/';
var path = 'one/two/three/αβγ';

var wipePath = (root, path, nofail) => {
    path
        .split('/')
        .map((x, i, a) => {
            return root + a.slice(0, i + 1).join('/');
        })
        .reverse()
        .map((dir) => {
            try {
                fs.rmdirSync(dir);
            } catch (e) {
                if (!nofail) {
                    throw e;
                }
            }
        });
};

var testSync = () => new Promise((resolve, reject) => {
    try {
        wipePath(dname, path + '/' + path, true);
        fs.rmdirSync(dname);
    } catch (e) {
    }

    try {
        fs.mkdirSync(dname);

        fs.mkdirSync(dname, { recursive: true });
        fs.mkdirSync(dname + '/', { recursive: true });
        fs.mkdirSync(dname + '////', { recursive: true });

        fs.mkdirSync(dname + path, { recursive: true });
        wipePath(dname, path);

        fs.mkdirSync(dname + '////' + path + '////' + path + '////', { recursive: true });
        wipePath(dname, path + '/' + path);

        try {
            fs.mkdirSync(dname + path, { recursive: true, mode: 0 });
        } catch (e) {
            if (e.code != 'EACCES') {
                reject(e);
            }
        }
        wipePath(dname, path, true);

        try {
            fs.mkdirSync(dname + path, { recursive: true });
            fs.writeFileSync(dname + path + '/one', 'not dir');
            fs.mkdirSync(dname + path + '/' + path, { recursive: true });
        } catch (e) {
            if (e.code != 'ENOTDIR') {
                reject(e);
            }
        }
        fs.unlinkSync(dname + path + '/one');
        wipePath(dname, path);

        fs.rmdirSync(dname);
        resolve();
    } catch (e) {
        reject(e);
    }
});

Promise.resolve()
.then(testSync)
.then(() => {
    console.log('test recursive fs.mkdirSync');
})
.catch((e) => {
    console.log('test failed recursive fs.mkdirSync', JSON.stringify(e));
})
