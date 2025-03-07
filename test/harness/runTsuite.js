async function run(tlist) {
    function validate(t, r, i) {
        if (r.status == "fulfilled" && r.value === "SKIPPED") {
            return true;
        }

        if (r.status == "fulfilled" && !t[i].exception) {
            return r.value === "SUCCESS";
        }

        if (r.status == "rejected" && t[i].exception) {
            return true;
        }

        if (r.status == "rejected" && t[i].optional) {
            return r.reason.toString().startsWith("InternalError: not implemented");
        }

        return false;
    }

    function map(ts) {
        return ts.tests.map(t => {
            try {
                if (t.skip && t.skip()) {
                    return Promise.resolve("SKIPPED");
                }

                let prepare_args = ts.prepare_args ? ts.prepare_args
                                                   : default_prepare_args;

                return ts.T(prepare_args(t, ts.opts ? ts.opts : {}));

            } catch (e) {
                return Promise.reject(e);
            }
        });
    }

    for (let k = 0; k < tlist.length; k++) {
        let ts = tlist[k];

        if (ts.skip && ts.skip()) {
            continue;
        }

        let results = await Promise.allSettled(map(ts));
        let r = results.map((r, i) => { r.passed = validate(ts.tests, r, i); return r; });

        let passed = r.filter(r => r.passed).length;
        if (passed != ts.tests.length) {
            r.forEach((r, i) => {
                if (!r.passed) {
                    console.log(`   ${JSON.stringify(ts.tests[i])}\n    with reason: ${r.reason}`);
                }
            })

            console.log(`${ts.name} FAILED: [${passed}/${ts.tests.length}]`);
        }
    }
}

function default_prepare_args(args, default_opts) {
    let params = merge({}, default_opts);
    params = merge(params, args);

    return params;
}

function merge(to, from) {
    let r = Object.assign(Array.isArray(to) ? [] : {}, to);
    Object.keys(from).forEach(v => {
        if (typeof r[v] == 'object' && typeof from[v] == 'object') {
            r[v] = merge(r[v], from[v]);

        } else if (typeof from[v] == 'object' && from[v] !== null) {
            if (Buffer.isBuffer(from[v])) {
                r[v] = Buffer.from(from[v]);

            } else if (from[v] instanceof Uint8Array) {
                r[v] = new Uint8Array(from[v]);

            } else if (from[v] instanceof ArrayBuffer) {
                r[v] = new ArrayBuffer(from[v].byteLength);

            } else {
                r[v] = Object.assign(Array.isArray(from[v]) ? [] : {}, from[v]);
            }

        } else {
            r[v] = from[v];
        }
    })

    return r;
}

