/*---
includes: [compatFs.js, compatBuffer.js, runTsuite.js]
flags: [async]
---*/

function p(args, default_opts) {
    let params = Object.assign({}, default_opts, args);

    if (params.args) {
        let fname = params.args[0];

        if (fname[0] == '@') {
            let gen = `${test_dir}/fs_test_${Math.round(Math.random() * 1000000)}`;
            params.args = params.args.map(v => v);
            params.args[0] = gen + fname.slice(1);
        }
    }

    return params;
}

function promisify(f) {
    return function (...args) {
        return new Promise((resolve, reject) => {
            function callback(err, result) {
                if (err) {
                    return reject(err);
                } else {
                    resolve(result);
                }
            }

        args.push(callback);
        f.apply(this, args);
       });
    };
}

async function method(name, params) {
    let data = null;

    switch (params.type) {
    case "sync":
        data = fs[name + "Sync"].apply(null, params.args);
        break;

    case "callback":
        data = await promisify(fs[name]).apply(null, params.args);
        break;

    case "promise":
        data = await fs.promises[name].apply(null, params.args);
        break;
    }

    return data;
}

async function readfile_test(params) {
    let data = await method("readFile", params).catch(e => ({error:e}));

    if (params.slice && !data.error) {
        data = data.slice.apply(data, params.slice);
    }

    if (params.check) {
        if (!params.check(data, params)) {
            throw Error(`readFile failed check`);
        }

    } else if (params.exception) {
        throw data.error;

    } else {
        let success = true;
        if (data instanceof Buffer) {
            if (data.compare(params.expected) != 0) {
                success = false;
            }

        } else if (data != params.expected) {
            success = false;
        }

        if (!success) {
            throw Error(`readFile unexpected data`);
        }
    }

    return 'SUCCESS';
}

let readfile_tests = () => [
    { args: ["test/fs/utf8"], expected: Buffer.from("αβZγ") },
    { args: [Buffer.from("@test/fs/utf8").slice(1)], expected: Buffer.from("αβZγ") },
    { args: ["test/fs/utf8", "utf8"], expected: "αβZγ" },
    { args: ["test/fs/utf8", {encoding: "utf8", flags:"r+"}], expected: "αβZγ" },
    { args: ["test/fs/nonexistent"],
      check: (err, params) => {
          let e = err.error;

          if (e.syscall != 'open') {
              throw Error(`${e.syscall} unexpected syscall`);
          }

          if (e.code != "ENOENT") {
              throw Error(`${e.code} unexpected code`);
          }

          if (e.path != "test/fs/nonexistent") {
              throw Error(`${e.path} unexpected path`);
          }

          return true;
      } },

    { args: ["test/fs/non_utf8", "utf8"], expected: "��" },
    { args: ["test/fs/non_utf8", {encoding: "hex"}], expected: "8080" },
    { args: ["test/fs/non_utf8", "base64"], expected: "gIA=" },
    { args: ["test/fs/ascii", "utf8"], expected: "x".repeat(600) },
    { args: ["test/fs/ascii", { encoding:"utf8", flags: "r+"}], expected: "x".repeat(600) },

    { args: [Buffer.from([0x80, 0x80])], exception: "Error: No such file or directory" },
    { args: ['x'.repeat(8192)], exception: "TypeError: \"path\" is too long" },

    { args: ["/proc/version"], slice:[0,5], expected: Buffer.from("Linux"),
      check: (data, params) => {

          if (data.error) {
              let e = data.error;
              if (e.syscall != 'open') {
                  throw Error(`${e.syscall} unexpected syscall`);
              }

              return true;
          }

          return data.compare(params.expected) == 0;
      } },
    { args: ["/proc/cpuinfo"],
      check: (data, params) => {

          if (data.error) {
              let e = data.error;
              if (e.syscall != 'open') {
                  throw Error(`${e.syscall} unexpected syscall`);
              }

              return true;
          }

          return /processor/.test(data);
      } },
];

let readFile_tsuite = {
    name: "fs readFile",
    skip: () => (!has_fs() || !has_buffer()),
    T: readfile_test,
    prepare_args: p,
    opts: { type: "callback" },
    get tests() { return readfile_tests() },
};

let readFileSync_tsuite = {
    name: "fs readFileSync",
    skip: () => (!has_fs() || !has_buffer()),
    T: readfile_test,
    prepare_args: p,
    opts: { type: "sync" },
    get tests() { return readfile_tests() },
};

let readFileP_tsuite = {
    name: "fsp readFile",
    skip: () => (!has_fs() || !has_buffer()),
    T: readfile_test,
    prepare_args: p,
    opts: { type: "promise" },
    get tests() { return readfile_tests() },
};

async function writefile_test(params) {
    let fname = params.args[0];

    try { fs.unlinkSync(fname); } catch (e) {}

    let data = await method("writeFile", params).catch(e => ({error:e}));

    if (!data) {
        data = fs.readFileSync(fname);
    }

    try { fs.unlinkSync(fname); } catch (e) {}

    if (params.check) {
        if (!params.check(data, params)) {
            throw Error(`writeFile failed check`);
        }

    } else if (params.exception) {
        throw data.error;

    } else {
        if (data.compare(params.expected) != 0) {
            throw Error(`writeFile unexpected data`);
        }
    }

    return 'SUCCESS';
}

let writefile_tests = () => [
    { args: ["@", Buffer.from(Buffer.alloc(4).fill(65).buffer, 1)],
      expected: Buffer.from("AAA") },
    { args: ["@", Buffer.from("XYZ"), "utf8"], expected: Buffer.from("XYZ") },
    { args: ["@", Buffer.from("XYZ"),  {encoding: "utf8", mode: 0o666}],
      expected: Buffer.from("XYZ") },
    { args: ["@", new DataView(Buffer.alloc(3).fill(66).buffer)],
      expected: Buffer.from("BBB") },
    { args: ["@", new Uint8Array(Buffer.from("ABCD"))],
      expected: Buffer.from("ABCD")},
    { args: ["@", "XYZ"], expected: Buffer.from("XYZ")},
    { args: ["@", "78797a", "hex"], expected: Buffer.from("xyz") },
    { args: ["@", "eHl6", "base64"], expected: Buffer.from("xyz") },
    { args: ["@", "eHl6", {encoding: "base64url"}], expected: Buffer.from("xyz"),
      optional: true },
    { args: ["@", Symbol("XYZ")], exception: "TypeError: Cannot convert a Symbol value to a string"},
    { args: ["/invalid_path", "XYZ"],
      check: (err, params) => {
          let e = err.error;

          if (e.syscall != 'open') {
              throw Error(`${e.syscall} unexpected syscall`);
          }

          if (e.code != "EACCES" && e.code != "EROFS") {
              throw Error(`${e.code} unexpected code`);
          }

          return true;
      } },
];

let writeFile_tsuite = {
    name: "fs writeFile",
    skip: () => (!has_fs() || !has_buffer()),
    T: writefile_test,
    prepare_args: p,
    opts: { type: "callback" },
    get tests() { return writefile_tests() },
};

let writeFileSync_tsuite = {
    name: "fs writeFileSync",
    skip: () => (!has_fs() || !has_buffer()),
    T: writefile_test,
    prepare_args: p,
    opts: { type: "sync" },
    get tests() { return writefile_tests() },
};

let writeFileP_tsuite = {
    name: "fsp writeFile",
    skip: () => (!has_fs() || !has_buffer()),
    T: writefile_test,
    prepare_args: p,
    opts: { type: "promise" },
    get tests() { return writefile_tests() },
};

async function append_test(params) {
    let fname = params.args[0];

    try { fs.unlinkSync(fname); } catch (e) {}

    let data = await method("appendFile", params).catch(e => ({error:e}));
    data = await method("appendFile", params).catch(e => ({error:e}));

    if (!data) {
        data = fs.readFileSync(fname);
    }

    try { fs.unlinkSync(fname); } catch (e) {}

    if (params.check) {
        if (!params.check(data, params)) {
            throw Error(`appendFile failed check`);
        }

    } else if (params.exception) {
        throw data.error;

    } else {
        if (data.compare(params.expected) != 0) {
            throw Error(`appendFile unexpected data`);
        }
    }

    return 'SUCCESS';
}

let append_tests = () => [
    { args: ["@", Buffer.from(Buffer.alloc(4).fill(65).buffer, 1)],
      expected: Buffer.from("AAAAAA") },
    { args: ["@", Buffer.from("XYZ"), "utf8"], expected: Buffer.from("XYZXYZ") },
    { args: ["@", Buffer.from("XYZ"),  {encoding: "utf8", mode: 0o666}],
      expected: Buffer.from("XYZXYZ") },
    { args: ["@", new DataView(Buffer.alloc(3).fill(66).buffer)],
      expected: Buffer.from("BBBBBB") },
    { args: ["@", new Uint8Array(Buffer.from("ABCD"))],
      expected: Buffer.from("ABCDABCD")},
    { args: ["@", "XYZ"], expected: Buffer.from("XYZXYZ")},
    { args: ["@", "78797a", "hex"], expected: Buffer.from("xyzxyz") },
    { args: ["@", "eHl6", "base64"], expected: Buffer.from("xyzxyz") },
    { args: ["@", "eHl6", {encoding: "base64url"}], expected: Buffer.from("xyzxyz"),
      optional: true },
    { args: ["@", Symbol("XYZ")], exception: "TypeError: Cannot convert a Symbol value to a string"},
    { args: ["/invalid_path", "XYZ"],
      check: (err, params) => {
          let e = err.error;

          if (e.syscall != 'open') {
              throw Error(`${e.syscall} unexpected syscall`);
          }

          if (e.code != "EACCES" && e.code != "EROFS") {
              throw Error(`${e.code} unexpected code`);
          }

          return true;
      } },
];

let appendFile_tsuite = {
    name: "fs appendFile",
    skip: () => (!has_fs() || !has_buffer()),
    T: append_test,
    prepare_args: p,
    opts: { type: "callback" },
    get tests() { return append_tests() },
};

let appendFileSync_tsuite = {
    name: "fs appendFileSync",
    skip: () => (!has_fs() || !has_buffer()),
    T: append_test,
    prepare_args: p,
    opts: { type: "sync" },
    get tests() { return append_tests() },
};

let appendFileP_tsuite = {
    name: "fsp appendFile",
    skip: () => (!has_fs() || !has_buffer()),
    T: append_test,
    prepare_args: p,
    opts: { type: "promise" },
    get tests() { return append_tests() },
};

async function realpath_test(params) {
    let data = await method("realpath", params);

    if (!params.check(data)) {
        throw Error(`realpath failed check`);
    }

    return 'SUCCESS';
}

let realpath_tests = () => [
    { args: ["test/fs/.."],
      check: (data) => data.endsWith("test") },
    { args: ["test/fs/ascii", {encoding:'buffer'}],
      check: (data) => data instanceof Buffer },
];

let realpath_tsuite = {
    name: "fs realpath",
    skip: () => (!has_fs() || !has_buffer()),
    T: realpath_test,
    prepare_args: p,
    opts: { type: "callback" },
    get tests() { return realpath_tests() },
};

let realpathSync_tsuite = {
    name: "fs realpathSync",
    skip: () => (!has_fs() || !has_buffer()),
    T: realpath_test,
    prepare_args: p,
    opts: { type: "sync" },
    get tests() { return realpath_tests() },
};

let realpathP_tsuite = {
    name: "fsp realpath",
    skip: () => (!has_fs() || !has_buffer()),
    T: realpath_test,
    prepare_args: p,
    opts: { type: "promise" },
    get tests() { return realpath_tests() },
};

async function method_test(params) {
    if (params.init) {
        params.init(params);
    }

    let ret = await method(params.method, params).catch(e => ({error:e}));

    if (params.check && !params.check(ret, params)) {
        throw Error(`${params.method} failed check`);
    }

    return 'SUCCESS';
}

function contains(arr, elts) {
    return elts.every(el => {
        let r = arr.some(v => el == v);

        if (!r) {
            throw Error(`${el} is not found`);
        }

        return r;
    });
}

let stat_tests = () => [
    { args: ["/invalid_path"],
      check: (err, params) => {
          let e = err.error;

          if (e.syscall != params.method) {
              throw Error(`${e.syscall} unexpected syscall`);
          }

          if (e.code != "ENOENT") {
              throw Error(`${e.code} unexpected code`);
          }

          return true;
      } },

    { args: ["@_link"],
      init: (params) => {
        let lname = params.args[0];
        let fname = lname.slice(0, -5);

        /* making symbolic link. */

        try { fs.unlinkSync(fname); fs.unlinkSync(lname); } catch (e) {}

        fs.writeFileSync(fname, fname);

        fname = fs.realpathSync(fname);
        fs.symlinkSync(fname, lname);
      },

      check: (st, params) => {
          switch (params.method) {
          case "stat":
              if (!st.isFile()) {
                  throw Error(`${params.args[0]} is not a file`);
              }

              break;

          case "lstat":
              if (!st.isSymbolicLink()) {
                  throw Error(`${params.args[0]} is not a link`);
              }

              break;
          }

          return true;
      } },

    { args: ["test/fs/ascii"],
      check: (st) => contains(Object.keys(st),
                              [ "atime", "atimeMs", "birthtime", "birthtimeMs",
                                "blksize", "blocks", "ctime", "ctimeMs", "dev",
                                "gid", "ino", "mode", "mtime", "mtimeMs","nlink",
                                "rdev", "size", "uid" ]) },

    { args: ["test/fs/ascii"],
      check: (st) => Object.keys(st).every(p => {
        let v = st[p];
        if (p == 'atime' || p == 'ctime' || p == 'mtime' || p == 'birthtime') {
            if (!(v instanceof Date)) {
                throw Error(`${p} is not an instance of Date`);
            }

            return true;
        }

        if ((typeof v) != 'number') {
            throw Error(`${p} is not an instance of Number`);
        }

        return true;
      }) },

    { args: ["test/fs/ascii"],
      check: (st) => ['atime', 'birthtime', 'ctime', 'mtime'].every(p => {
          let date = st[p].valueOf();
          let num = st[p + 'Ms'];

          if (Math.abs(date - num) > 1) {
            throw Error(`${p}:${date} != ${p+'Ms'}:${num}`);
          }

          return true;
      }) },

    { args: [test_dir],
      check: (st) => ['isBlockDevice',
                      'isCharacterDevice',
                      'isDirectory',
                      'isFIFO',
                      'isFile',
                      'isSocket',
                      'isSymbolicLink'].every(m => {

          let r = st[m]();
          if (!(r == (m == 'isDirectory'))) {
            throw Error(`${m} is ${r}`);
          }

          return true;
      }) },
];

let stat_tsuite = {
    name: "fs stat",
    skip: () => (!has_fs() || !has_fs_symbolic_link() || !has_buffer()),
    T: method_test,
    prepare_args: p,
    opts: { type: "callback", method: "stat" },
    get tests() { return stat_tests() },
};

let statSync_tsuite = {
    name: "fs statSync",
    skip: () => (!has_fs() || !has_fs_symbolic_link() || !has_buffer()),
    T: method_test,
    prepare_args: p,
    opts: { type: "sync", method: "stat" },
    get tests() { return stat_tests() },
};

let statP_tsuite = {
    name: "fsp stat",
    skip: () => (!has_fs() || !has_fs_symbolic_link() || !has_buffer()),
    T: method_test,
    prepare_args: p,
    opts: { type: "promise", method: "stat" },
    get tests() { return stat_tests() },
};

let lstat_tsuite = {
    name: "fs lstat",
    skip: () => (!has_fs() || !has_fs_symbolic_link() || !has_buffer()),
    T: method_test,
    prepare_args: p,
    opts: { type: "callback", method: "lstat" },
    get tests() { return stat_tests() },
};

let lstatSync_tsuite = {
    name: "fs lstatSync",
    skip: () => (!has_fs() || !has_fs_symbolic_link() || !has_buffer()),
    T: method_test,
    prepare_args: p,
    opts: { type: "sync", method: "lstat" },
    get tests() { return stat_tests() },
};

let lstatP_tsuite = {
    name: "fsp lstat",
    skip: () => (!has_fs() || !has_fs_symbolic_link() || !has_buffer()),
    T: method_test,
    prepare_args: p,
    opts: { type: "promise", method: "lstat" },
    get tests() { return stat_tests() },
};

let open_check = (fh, params) => {
    if (params.type == 'promise') {

        try {
            if (typeof fh.fd != 'number') {
                throw Error(`filehandle.fd:${fh.fd} is not an instance of Number`);
            }

            ['read', 'write', 'close', 'valueof'].every(v => {
                if (typeof fh[v] != 'function') {
                    throw Error(`filehandle.close:${fh[v]} is not an instance of function`);
                }
            });

            let mode = fs.fstatSync(fh.fd).mode & 0o777;
            if (params.mode && params.mode != mode) {
                throw Error(`opened mode ${mode} != ${params.mode}`);
            }

        } finally {
            fh.close();
        }

    } else {

        try {
            if (typeof fh != 'number') {
                throw Error(`fd:${fh} is not an instance of Number`);
            }

            let mode = fs.fstatSync(fh).mode & 0o777;
            if (params.mode && params.mode != mode) {
                throw Error(`opened mode ${mode} != ${params.mode}`);
            }

        } finally {
            fs.closeSync(fh);
        }
    }

    return true;
};

let open_tests = () => [
    {
      args: ["test/fs/ascii"],
      check: open_check,
    },

    {
      args: ["@", 'w', 0o600],
      mode: 0o600,
      check: open_check,
    },

    {
      args: ["@", 'a', 0o700],
      mode: 0o700,
      check: open_check,
    },

    {
      args: ["@", 'r'],
      check: (err, params) => {
          let e = err.error;

          if (e.syscall != params.method) {
              throw Error(`${e.syscall} unexpected syscall`);
          }

          if (e.code != "ENOENT") {
              throw Error(`${e.code} unexpected code`);
          }

          return true;
      },
    },

    {
      args: ["/invalid_path"],
      check: (err, params) => {
          let e = err.error;

          if (e.syscall != params.method) {
              throw Error(`${e.syscall} unexpected syscall`);
          }

          if (e.code != "ENOENT") {
              throw Error(`${e.code} unexpected code`);
          }

          return true;
      },
    },
];

let openSync_tsuite = {
    name: "fs openSync",
    skip: () => (!has_fs() || !has_buffer()),
    T: method_test,
    prepare_args: p,
    opts: { type: "sync", method: "open" },
    get tests() { return open_tests() },
};

let openP_tsuite = {
    name: "fsp open",
    skip: () => (!has_fs() || !has_buffer()),
    T: method_test,
    prepare_args: p,
    opts:  { type: "promise", method: "open" },
    get tests() { return open_tests() },
};

let close_tests = () => [

    {
      args: [ fs.openSync("test/fs/ascii") ],
      check: (undef, params) => undef === undefined,
    },

    {
      args: [ (() => { let fd = fs.openSync("test/fs/ascii"); fs.closeSync(fd); return fd})() ],
      check: (err, params) => {
          let e = err.error;

          if (e.syscall != params.method) {
              throw Error(`${e.syscall} unexpected syscall`);
          }

          if (e.code != "EBADF") {
              throw Error(`${e.code} unexpected code`);
          }

          return true;
      },
    },
];

let closeSync_tsuite = {
    name: "fs closeSync",
    skip: () => (!has_fs() || !has_buffer()),
    T: method_test,
    prepare_args: p,
    opts: { type: "sync", method: "close" },
    get tests() { return close_tests() },
};

function read_test(params) {
    let fd, err;

    let fn = `${test_dir}/fs_read_test_${Math.round(Math.random() * 1000000)}`;
    let out = [];

    fs.writeFileSync(fn, params.content);

    try {
        fd = fs.openSync(fn);

        let buffer = Buffer.alloc(4);
        buffer.fill('#');

        for (var i = 0; i < params.read.length; i++) {
            let args = params.read[i].map(v => v);
            args.unshift(buffer);
            args.unshift(fd);

            let bytesRead = fs.readSync.apply(null, args);

            out.push([bytesRead, Buffer.from(buffer)]);
        }

    } catch (e) {
        if (!e.syscall && !params.check) {
            throw e;
        }

        err = e;

    } finally {
        fs.closeSync(fd);
    }

    if (!err && params.expected) {
        let expected = params.expected;

        if (out.length != expected.length) {
            throw Error(`unexpected readSync number of outputs ${out.length} != ${expected.length}`);
        }

        for (var i = 0; i < expected.length; i++) {
            if (expected[i][0] != out[i][0]) {
                throw Error(`unexpected readSync bytesRead:${out[i][0]} != ${expected[i][0]}`);
            }

            if (expected[i][1].compare(out[i][1]) != 0) {
                throw Error(`unexpected readSync buffer:${out[i][1]} != ${expected[i][1]}`);
            }
        }

    }

    if (params.check && !params.check(err, params)) {
        throw Error(`${params.method} failed check`);
    }

    return 'SUCCESS';
}

async function readFh_test(params) {
    let fh, err;

    let fn = `${test_dir}/fs_read_test_${Math.round(Math.random() * 1000000)}`;
    let out = [];

    fs.writeFileSync(fn, params.content);

    try {
        fh = await fs.promises.open(fn);

        let buffer = Buffer.alloc(4);
        buffer.fill('#');

        for (var i = 0; i < params.read.length; i++) {
            let args = params.read[i].map(v => v);
            args.unshift(buffer);

            let bs = await fh.read.apply(fh, args);

            out.push([bs.bytesRead, Buffer.from(bs.buffer)]);
        }

    } catch (e) {
        if (!e.syscall && !params.check) {
            throw e;
        }

        err = e;

    } finally {
        await fh.close();
    }

    if (!err && params.expected) {
        let expected = params.expected;

        if (out.length != expected.length) {
            throw Error(`unexpected read number of outputs ${out.length} != ${expected.length}`);
        }

        for (var i = 0; i < expected.length; i++) {
            if (expected[i][0] != out[i][0]) {
                throw Error(`unexpected read bytesRead:${out[i][0]} != ${expected[i][0]}`);
            }

            if (expected[i][1].compare(out[i][1]) != 0) {
                throw Error(`unexpected read buffer:${out[i][1]} != ${expected[i][1]}`);
            }
        }

    }

    if (params.check && !params.check(err, params)) {
        throw Error(`${params.method} failed check`);
    }

    return 'SUCCESS';
}

let read_tests = () => [

    {
        content: "ABC",
        read: [ [0, 3], ],
        expected: [ [3, Buffer.from("ABC#")], ],
    },

    {
        content: "ABC",
        read: [ [1, 2], ],
        expected: [ [2, Buffer.from("#AB#")], ],
    },

    {
        content: "ABC",
        read: [ [1, 2, 1], ],
        expected: [ [2, Buffer.from("#BC#")], ],
    },

    {
        content: "__ABCDE",
        read: [
                [0, 4],
                [0, 4],
                [2, 2, 0],
                [0, 4, null],
              ],
        expected: [
                    [4, Buffer.from("__AB")],
                    [3, Buffer.from("CDEB")],
                    [2, Buffer.from("CD__")],
                    [0, Buffer.from("CD__")],
                  ],
    },

    {
        content: "ABC",
        read: [ [0, 5], ],
        check: (err, params) => {
            if (err.name != "RangeError") {
                throw Error(`${err.code} unexpected exception`);
            }

            return true;
        },
    },

    {
        content: "ABC",
        read: [ [2, 3], ],
        check: (err, params) => {
            if (err.name != "RangeError") {
                throw Error(`${err.code} unexpected exception`);
            }

            return true;
        },
    },

];

let readSync_tsuite = {
    name: "fs readSync",
    skip: () => (!has_fs() || !has_buffer()),
    T: read_test,
    prepare_args: p,
    opts: {},
    get tests() { return read_tests() },
};

let readFh_tsuite = {
    name: "fh read",
    skip: () => (!has_fs() || !has_buffer()),
    T: readFh_test,
    prepare_args: p,
    opts: {},
    get tests() { return read_tests() },
};

function write_test(params) {
    let fd, err;

    try {
        fd = fs.openSync.apply(null, params.args);

        for (var i = 0; i < params.write.length; i++) {
            let args = params.write[i].map(v => v);
            args.unshift(fd);

            let bytesWritten = fs.writeSync.apply(null, args);

            if (params.written && bytesWritten != params.written) {
                throw Error(`bw.bytesWritten unexpected value:${bw.bytesWritten}`);
            }
        }

    } catch (e) {
        if (!e.syscall && !params.check) {
            throw e;
        }

        err = e;

    } finally {
        fs.closeSync(fd);
    }

    if (!err && params.expected) {
        let data = fs.readFileSync(params.args[0]);

        if (data.compare(params.expected) != 0) {
            throw Error(`fh.write unexpected data:${data}`);
        }
    }

    if (params.check && !params.check(err, params)) {
        throw Error(`${params.method} failed check`);
    }

    return 'SUCCESS';
}

async function writeFh_test(params) {
    let fh, err;

    try {
        fh = await fs.promises.open.apply(null, params.args);

        for (var i = 0; i < params.write.length; i++) {
            let bw = await fh.write.apply(fh, params.write[i]);

            if (params.written && bw.bytesWritten != params.written) {
                throw Error(`bw.bytesWritten unexpected value:${bw.bytesWritten}`);
            }

            if (params.buffer
                && (typeof params.buffer == 'string'
                    && params.buffer != bw.buffer
                    || typeof params.buffer == 'object'
                       && params.buffer.compare(bw.buffer) != 0))
            {
                throw Error(`bw.buffer unexpected value:${bw.buffer}`);
            }
        }

    } catch (e) {
        if (!e.syscall && !params.check) {
            throw e;
        }

        err = e;

    } finally {
        await fh.close();
    }

    if (!err && params.expected) {
        let data = fs.readFileSync(params.args[0]);

        if (data.compare(params.expected) != 0) {
            throw Error(`fh.write unexpected data:${data}`);
        }
    }

    if (params.check && !params.check(err, params)) {
        throw Error(`${params.method} failed check`);
    }

    return 'SUCCESS';
}

let write_tests = () => [

    {
        args: ["@", 'w'],
        write: [ ["ABC", undefined], ["DE", null], ["F"], ],
        expected: Buffer.from("ABCDEF"),
    },

    {
        args: ["@", 'w'],
        write: [ ["XXXXXX"], ["YYYY", 1], ["ZZ", 2], ],
        expected: Buffer.from("XYZZYX"),
    },

    {
        args: ["@", 'w'],
        write: [ ["ABC", null, 'utf8'] ],
        written: 3,
        buffer: 'ABC',
        expected: Buffer.from("ABC"),
    },

    {
        args: ["test/fs/ascii"],
        write: [ ["ABC"] ],
        check: (err, params) => {
            let e = err;

            if (e.syscall != 'write') {
                throw Error(`${e.syscall} unexpected syscall`);
            }

            if (e.code != "EBADF") {
                throw Error(`${e.code} unexpected code`);
            }

            return true;
        },
    },

    {
        args: ["@", 'w'],
        write: [ [Buffer.from("ABC"), 0, 3],
                 [Buffer.from("DE"), 0, 2, null],
                 [Buffer.from("F"), 0, 1], ],
        expected: Buffer.from("ABCDEF"),
    },

    {
        args: ["@", 'w'],
        write: [ [Buffer.from("__XXXXXX"), 2],
                 [Buffer.from("__YYYY__"), 2, 4, 1],
                 [Buffer.from("ZZ"), 0, 2, 2], ],
        expected: Buffer.from("XYZZYX"),
    },

    {
        args: ["@", 'w'],
        write: [ [Buffer.from("__ABC__"), 2, 3] ],
        written: 3,
        buffer: Buffer.from('__ABC__'),
        expected: Buffer.from("ABC"),
    },

    {
        args: ["@", 'w'],
        write: [ [Buffer.from("__ABC__"), 7] ],
        written: 0,
    },

    {
        args: ["@", 'w'],
        write: [ [Buffer.from("__ABC__"), 8] ],
        check: (err, params) => {
            if (err.name != "RangeError") {
                throw Error(`${err.code} unexpected exception`);
            }

            return true;
        },
    },

    {
        args: ["@", 'w'],
        write: [ [Buffer.from("__ABC__"), 7, 1] ],
        check: (err, params) => {
            if (err.name != "RangeError") {
                throw Error(`${err.code} unexpected exception`);
            }

            return true;
        },
    },
];

let writeSync_tsuite = {
    name: "fs writeSync",
    skip: () => (!has_fs() || !has_buffer()),
    T: write_test,
    prepare_args: p,
    opts: {},
    get tests() { return write_tests() },
};

let writeFh_tsuite = {
    name: "fh write",
    skip: () => (!has_fs() || !has_buffer()),
    T: writeFh_test,
    prepare_args: p,
    opts: {},
    get tests() { return write_tests() },
};

run([
    readFile_tsuite,
    readFileSync_tsuite,
    readFileP_tsuite,
    writeFile_tsuite,
    writeFileSync_tsuite,
    writeFileP_tsuite,
    appendFile_tsuite,
    appendFileSync_tsuite,
    appendFileP_tsuite,
    realpath_tsuite,
    realpathSync_tsuite,
    realpathP_tsuite,
    stat_tsuite,
    statSync_tsuite,
    statP_tsuite,
    lstat_tsuite,
    lstatSync_tsuite,
    lstatP_tsuite,
    openSync_tsuite,
    openP_tsuite,
    readSync_tsuite,
    readFh_tsuite,
    writeSync_tsuite,
    writeFh_tsuite,
    closeSync_tsuite,
])
.then($DONE, $DONE);
