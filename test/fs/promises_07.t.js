/*---
includes: [compareArray.js, compatFs.js]
flags: [async]
---*/

var dname = `${test_dir}/fs_promises_07`;
var dname_utf8 = `${test_dir}/fs_promises_αβγ_07`;
var fname = (d) => d + '/fs_promises_07_file';
var lname = (d) => d + '/fs_promises_07_link';
var cname = (d) => d + '/fs_promises_αβγ_07_dir';


var dir_test = [cname(''), lname(''), fname('')].map((x) => x.substring(1));
var match = (entry) => {
    var idx = dir_test.indexOf(entry.name);

    try {
        switch (idx) {
        case 0:
            return entry.isDirectory();
        case 1:
            return entry.isSymbolicLink();
        case 2:
            return entry.isFile();
        default:
            return false;
        }
    } catch (e) {
        if (e.name == 'InternalError') {
            return true;
        }

        throw e;
    }
};

let stages = [];

var testSync = () => new Promise((resolve, reject) => {
    try {
        try { fs.rmdirSync(cname(dname)); } catch (e) {}
        try { fs.rmdirSync(cname(dname_utf8)); } catch (e) {}
        try { fs.unlinkSync(lname(dname)); } catch (e) {}
        try { fs.unlinkSync(lname(dname_utf8)); } catch (e) {}
        try { fs.unlinkSync(fname(dname)); } catch (e) {}
        try { fs.unlinkSync(fname(dname_utf8)); } catch (e) {}
        try { fs.rmdirSync(dname); } catch (e) {}
        try { fs.rmdirSync(dname_utf8); } catch (e) {}

        try {
            fs.readdirSync(dname);
            throw new Error('fs.readdirSync - error 0');

        } catch (e) {
            if (e.code != 'ENOENT') {
                // njs: e.syscall == 'opendir'
                // node: e.syscall == 'scandir'
                throw e;
            }
        }

        fs.mkdirSync(dname);
        fs.mkdirSync(dname_utf8);
        fs.writeFileSync(fname(dname), fname(dname));
        fs.writeFileSync(fname(dname_utf8), fname(dname_utf8));
        fs.symlinkSync(fname('.'), lname(dname));
        fs.symlinkSync(fname('.'), lname(dname_utf8));
        fs.mkdirSync(cname(dname));
        fs.mkdirSync(cname(dname_utf8));

        var dir = fs.readdirSync(dname);
        var dir_utf8 = fs.readdirSync(dname_utf8);
        if (dir.length != dir_utf8.length || dir.length != 3) {
            throw new Error('fs.readdirSync - error 1');
        }

        var test = dir.filter((x) => !dir_test.includes(x));
        if (test.length != 0) {
            throw new Error('fs.readdirSync - error 2');
        }

        var test = dir_utf8.filter((x) => !dir_test.includes(x));
        if (test.length != 0) {
            throw new Error('fs.readdirSync - error 3');
        }

        var dir = fs.readdirSync(dname, { withFileTypes: true });
        var dir_utf8 = fs.readdirSync(dname_utf8, { withFileTypes: true });
        if (dir.length != dir_utf8.length || dir.length != 3) {
            throw new Error('fs.readdirSync - error 4');
        }

        var test = dir.filter((x) => !match(x));
        if (test.length != 0) {
            throw new Error('fs.readdirSync - error 5');
        }

        var test = dir_utf8.filter((x) => !match(x));
        if (test.length != 0) {
            throw new Error('fs.readdirSync - error 6');
        }

        var dir_buffer = fs.readdirSync(dname, {encoding:'buffer'});
        if (dir_buffer.length != 3 || !(dir_buffer[0] instanceof Buffer)) {
            throw new Error('fs.readdirSync - error 7');
        }

        var dir_buffer_types = fs.readdirSync(dname, {encoding:'buffer', withFileTypes: true});
        if (dir_buffer_types.length != 3 || !(dir_buffer_types[0].name instanceof Buffer)) {
            throw new Error('fs.readdirSync - error 8');
        }

        stages.push("readdirSync");

        resolve();

    } catch (e) {
        reject(e);
    }
});


var testCallback = () => new Promise((resolve, reject) => {
    try {
        try { fs.rmdirSync(cname(dname)); } catch (e) {}
        try { fs.unlinkSync(lname(dname)); } catch (e) {}
        try { fs.unlinkSync(fname(dname)); } catch (e) {}
        try { fs.rmdirSync(dname); } catch (e) {}

        fs.readdir(dname, (err, files) => {
            if (!err || err.code != 'ENOENT') {
                reject(new Error('fs.readdir - error 1'));
            }

            try {
                fs.mkdirSync(dname);
                fs.writeFileSync(fname(dname), fname(dname));
                fs.symlinkSync(fname('.'), lname(dname));
                fs.mkdirSync(cname(dname));

            } catch (e) {
                reject(e);
            }

            fs.readdir(dname, (err, dir) => {
                if (err) {
                    reject(err);
                }

                if (dir.length != 3) {
                    reject(new Error('fs.readdir - error 2'));
                }

                var test = dir.filter((x) => !dir_test.includes(x));
                if (test.length != 0) {
                    reject(new Error('fs.readdir - error 3'));
                }

                fs.readdir(dname, { withFileTypes: true }, (err, dir) => {
                    if (err) {
                        reject(err);
                    }

                    if (dir.length != 3) {
                        reject(new Error('fs.readdir - error 4'));
                    }

                    var test = dir.filter((x) => !match(x));
                    if (test.length != 0) {
                        reject(new Error('fs.readdir - error 5'));
                    }

                    stages.push("readdir");

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
    try { fs.rmdirSync(cname(dname)); } catch (e) {}
    try { fs.unlinkSync(lname(dname)); } catch (e) {}
    try { fs.unlinkSync(fname(dname)); } catch (e) {}
    try { fs.rmdirSync(dname); } catch (e) {}
})
.then(() => fsp.readdir(dname)
               .then(() => { throw new Error('fsp.readdir - error 1'); }))
.catch((e) => {
    if (e.code != 'ENOENT') {
        throw e;
    }
})
.then(() => {
    fs.mkdirSync(dname);
    fs.writeFileSync(fname(dname), fname(dname));
    fs.symlinkSync(fname('.'), lname(dname));
    fs.mkdirSync(cname(dname));
})
.then(() => fsp.readdir(dname))
.then((dir) => {
    if (dir.length != 3) {
        throw new Error('fsp.readdir - error 2');
    }

    var test = dir.filter((x) => !dir_test.includes(x));
    if (test.length != 0) {
        throw new Error('fsp.readdir - error 3');
    }
})
.then(() => fsp.readdir(dname, { withFileTypes: true }))
.then((dir) => {
    if (dir.length != 3) {
        throw new Error('fsp.readdir - error 4');
    }

    var test = dir.filter((x) => !match(x));
    if (test.length != 0) {
        throw new Error('fsp.readdir - error 5');
    }
})
.then(() => {
    stages.push("fsp.readdir");
})

let p = Promise.resolve()
if (has_fs() && has_fs_symbolic_link()) {
    p = p
        .then(testSync)
        .then(testCallback)
        .then(testFsp)
        .then(() => assert.compareArray(stages, ['readdirSync', 'readdir', 'fsp.readdir']))
}

p.then($DONE, $DONE);
