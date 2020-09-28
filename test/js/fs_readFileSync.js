var fs = require('fs');

var argv = process.argv.slice(2);
var fname = argv[0];

var options = (() => {
    var encoding = argv[1];
    var flags = argv[2];

    if (encoding && flags) {
        return {encoding, flags};

    } else if (encoding) {
        return encoding;
    }

    return undefined;
})();

function type(v) {
    if (v instanceof Buffer) {
        return 'Buffer';
    }

    return typeof v;
}

var data;

try {
    if (options) {
        var path = Buffer.from(`@${fname}`).slice(1);
        data = fs.readFileSync(path, options);

    } else {
        data = fs.readFileSync(fname);
    }

} catch (e) {
    console.log(JSON.stringify(e));
}

console.log(String(data), type(data), data.length);
