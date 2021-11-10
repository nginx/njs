var fs = require('fs');

async function run(tlist) {
    function validate(t, r, i) {
        if (r.status == "fulfilled") {
            return r.value === "SUCCESS";
        }

        if (r.status == "rejected" && t[i].exception) {
            if (process.argv[2] === '--match-exception-text') {
                /* is not compatible with node.js format */
                return r.reason.toString().startsWith(t[i].exception);
            }

            return true;
        }

        if (r.status == "rejected" && t[i].optional) {
            return r.reason.toString().startsWith("Error: No such file or directory");
        }

        return false;
    }

    for (let k = 0; k < tlist.length; k++) {
        let ts = tlist[k];
        let results = await Promise.allSettled(ts.tests.map(t => ts.T(ts.prepare_args(t, ts.opts))));
        let r = results.map((r, i) => validate(ts.tests, r, i));

        console.log(`${ts.name} ${r.every(v=>v == true) ? "SUCCESS" : "FAILED"}`);

        r.forEach((v, i) => {
            if (!v) {
                console.log(`FAILED ${i}: ${JSON.stringify(ts.tests[i])}\n    with reason: ${results[i].reason}`);
            }
        })
    }
}

function p(args, default_opts) {
    let params = Object.assign({}, default_opts, args);

    let fname = params.args[0];

    if (fname[0] == '@') {
        let gen = `build/test/fs_test_${Math.round(Math.random() * 1000000)}`;
        params.args = params.args.map(v => v);
        params.args[0] = gen + fname.slice(1);
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
        try {
            data = fs[name + "Sync"].apply(null, params.args);

        } catch (e) {
            if (!params.stringify) {
                throw e;
            }

            data = Buffer.from(JSON.stringify(e));
        }

        break;

    case "callback":
        data = await promisify(fs[name]).apply(null, params.args)
              .catch(e => {
                  if (!params.stringify) {
                      throw e;
                  }

                  return Buffer.from(JSON.stringify(e));
              });

        break;

    case "promise":
        data = await fs.promises[name].apply(null, params.args)
              .catch(e => {
                  if (!params.stringify) {
                      throw e;
                  }

                  return Buffer.from(JSON.stringify(e));
              });

        break;
    }

    return data;
}

async function read_test(params) {
    let data = await method("readFile", params);

    if (params.slice) {
        data = data.slice.apply(data, params.slice);
    }

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

    return 'SUCCESS';
}

let read_tests = [
    { args: ["test/fs/utf8"], expected: Buffer.from("αβZγ") },
    { args: [Buffer.from("@test/fs/utf8").slice(1)], expected: Buffer.from("αβZγ") },
    { args: ["test/fs/utf8", "utf8"], expected: "αβZγ" },
    { args: ["test/fs/utf8", {encoding: "utf8", flags:"r+"}], expected: "αβZγ" },
    { args: ["test/fs/nonexistent"], stringify: true,
      expected: Buffer.from('{"errno":2,"code":"ENOENT","path":"test/fs/nonexistent","syscall":"open"}'),
      exception: "Error: No such file or directory" },
    { args: ["test/fs/non_utf8", "utf8"], expected: "��" },
    { args: ["test/fs/non_utf8", {encoding: "hex"}], expected: "8080" },
    { args: ["test/fs/non_utf8", "base64"], expected: "gIA=" },
    { args: ["test/fs/ascii", "utf8"], expected: "x".repeat(600) },
    { args: ["test/fs/ascii", { encoding:"utf8", flags: "r+"}], expected: "x".repeat(600) },

    { args: [Buffer.from([0x80, 0x80])], exception: "Error: No such file or directory" },
    { args: ['x'.repeat(8192)], exception: "TypeError: \"path\" is too long" },

    { args: ["/proc/version"], slice:[0,5], expected: Buffer.from("Linux"), optional: true },
    { args: ["/proc/cpuinfo"], slice:[0,9], expected: Buffer.from("processor"), optional: true },
];

let readFile_tsuite = {
    name: "fs readFile",
    T: read_test,
    prepare_args: p,
    opts: { type: "callback" },
    tests: read_tests,
};

let readFileSync_tsuite = {
    name: "fs readFileSync",
    T: read_test,
    prepare_args: p,
    opts: { type: "sync" },
    tests: read_tests,
};

let readFileP_tsuite = {
    name: "fsp readFile",
    T: read_test,
    prepare_args: p,
    opts: { type: "promise" },
    tests: read_tests,
};

async function write_test(params) {
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

let write_tests = [
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
    T: write_test,
    prepare_args: p,
    opts: { type: "callback" },
    tests: write_tests,
};

let writeFileSync_tsuite = {
    name: "fs writeFileSync",
    T: write_test,
    prepare_args: p,
    opts: { type: "sync" },
    tests: write_tests,
};

let writeFileP_tsuite = {
    name: "fsp writeFile",
    T: write_test,
    prepare_args: p,
    opts: { type: "promise" },
    tests: write_tests,
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

let append_tests = [
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
    T: append_test,
    prepare_args: p,
    opts: { type: "callback" },
    tests: append_tests,
};

let appendFileSync_tsuite = {
    name: "fs appendFileSync",
    T: append_test,
    prepare_args: p,
    opts: { type: "sync" },
    tests: append_tests,
};

let appendFileP_tsuite = {
    name: "fsp appendFile",
    T: append_test,
    prepare_args: p,
    opts: { type: "promise" },
    tests: append_tests,
};

async function realpath_test(params) {
    let data = await method("realpath", params);

    if (!params.check(data)) {
        throw Error(`realpath failed check`);
    }

    return 'SUCCESS';
}

let realpath_tests = [
    { args: ["./build/test/.."],
      check: (data) => data.endsWith("build") },
    { args: ["./build/test/", {encoding:'buffer'}],
      check: (data) => data instanceof Buffer },
];

let realpath_tsuite = {
    name: "fs realpath",
    T: realpath_test,
    prepare_args: p,
    opts: { type: "callback" },
    tests: realpath_tests,
};

let realpathSync_tsuite = {
    name: "fs realpathSync",
    T: realpath_test,
    prepare_args: p,
    opts: { type: "sync" },
    tests: realpath_tests,
};

let realpathP_tsuite = {
    name: "fsp realpath",
    T: realpath_test,
    prepare_args: p,
    opts: { type: "promise" },
    tests: realpath_tests,
};

async function stat_test(params) {
    if (params.init) {
        params.init(params);
    }

    let stat = await method(params.method, params).catch(e => ({error:e}));

    if (params.check && !params.check(stat, params)) {
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

let stat_tests = [
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

    { args: ["./build/"],
      check: (st) => contains(Object.keys(st),
                              [ "atime", "atimeMs", "birthtime", "birthtimeMs",
                                "blksize", "blocks", "ctime", "ctimeMs", "dev",
                                "gid", "ino", "mode", "mtime", "mtimeMs","nlink",
                                "rdev", "size", "uid" ]) },

    { args: ["./build/"],
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

    { args: ["./build/"],
      check: (st) => ['atime', 'birthtime', 'ctime', 'mtime'].every(p => {
          let date = st[p].valueOf();
          let num = st[p + 'Ms'];

          if (Math.abs(date - num) > 1) {
            throw Error(`${p}:${date} != ${p+'Ms'}:${num}`);
          }

          return true;
      }) },

    { args: ["./build/"],
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
    T: stat_test,
    prepare_args: p,
    opts: { type: "callback", method: "stat" },
    tests: stat_tests,
};

let statSync_tsuite = {
    name: "fs statSync",
    T: stat_test,
    prepare_args: p,
    opts: { type: "sync", method: "stat" },
    tests: stat_tests,
};

let statP_tsuite = {
    name: "fsp stat",
    T: stat_test,
    prepare_args: p,
    opts: { type: "promise", method: "stat" },
    tests: stat_tests,
};

let lstat_tsuite = {
    name: "fs lstat",
    T: stat_test,
    prepare_args: p,
    opts: { type: "callback", method: "lstat" },
    tests: stat_tests,
};

let lstatSync_tsuite = {
    name: "fs lstatSync",
    T: stat_test,
    prepare_args: p,
    opts: { type: "sync", method: "lstat" },
    tests: stat_tests,
};

let lstatP_tsuite = {
    name: "fsp lstat",
    T: stat_test,
    prepare_args: p,
    opts: { type: "promise", method: "lstat" },
    tests: stat_tests,
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
]);
