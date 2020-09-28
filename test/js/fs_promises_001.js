var fs = require('fs').promises;
var fname = './build/test/fs_promises_001';

Promise.resolve()
.then(() => {
    return fs.writeFile(fname, fname);
})
.then((data) => {
    console.log('init ok', data === undefined);
})
.catch((e) => {
    console.log('init failed', e);
})
.then(() => {
    return fs.readFile(fname).then(fs.readFile);
})
.then((data) => {
    console.log('short circut ok', data == fname);
})
.catch((e) => {
    console.log('short circut failed', e);
})
.then(() => {
    var read = fs.readFile.bind(fs, fname, 'utf8');
    var write = fs.writeFile.bind(fs, fname);
    var append = fs.appendFile.bind(fs, fname);

    return write(fname).then(read).then(append).then(read);
})
.then((data) => {
    console.log('chain ok', data == (fname + fname));
})
.catch((e) => {
    console.log('chain failed', e);
})
.then(() => {
    // nodejs incompatible
    try {
        return fs.readFile();
    } catch (e) {
        console.log('error 1 ok', e instanceof TypeError)
    }
    try {
        return fs.writeFile();
    } catch (e) {
        console.log('error 2 ok', e instanceof TypeError)
    }
})
.then((data) => {
    console.log('errors ok');
})
.catch((e) => {
    console.log('errors failed - reject on bad args');
})
;
