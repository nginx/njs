var fs = require('fs');
var fname = './build/test/fs_appendFile';

var argv = process.argv.slice(2);

var data = (() => {
    var value = argv[0];
    var type = argv[1];
    var offset = argv[2] ? parseInt(argv[2]) : 0;

    switch (type) {
    case 'Buffer':
        return Buffer.from(Buffer.from(value).buffer, offset);
    case 'DataView':
        return new DataView(Buffer.from(value).buffer, offset);
    case 'Object':
        return {toString(){return value}};
    case 'String':
        return String(value);
    case 'Symbol':
        return Symbol(value);
    case 'Uint8Array':
        return new Uint8Array(Buffer.from(value).buffer, offset);
    default:
        throw new Error(`Unknown data type:${type}`);
    }
})();

var options = (() => {
    var encoding = argv[2];
    var mode = argv[3] ? parseInt(argv[3].slice(2), 8) : 0;

    if (encoding && mode) {
        return {encoding, mode};

    } else if (encoding) {
        return encoding;
    }

    return undefined;
})();

try { fs.unlinkSync(fname); } catch (e) {}

function done(e) {
    if (e) {throw e};
    var data = fs.readFileSync(fname);
    console.log(String(data));
}

function append(cb) {
    if (options) {
        var path = Buffer.from(`@${fname}`).slice(1);
        fs.appendFile(path, data, options, cb);

    } else {
        fs.appendFile(fname, data, cb);
    }
}

append((e) => {if (e) {throw e}; append(done);})
