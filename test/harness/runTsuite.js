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

    for (let k = 0; k < tlist.length; k++) {
        let ts = tlist[k];

        if (ts.skip && ts.skip()) {
            continue;
        }

        let results = await Promise.allSettled(ts.tests.map(t => ts.T(ts.prepare_args(t, ts.opts))));
        let r = results.map((r, i) => validate(ts.tests, r, i));

        r.forEach((v, i) => {
            assert.sameValue(v, true, `FAILED ${i}: ${JSON.stringify(ts.tests[i])}\n    with reason: ${results[i].reason}`);
        })
    }
}

function merge(to, from) {
    let r = Object.assign(Array.isArray(to) ? [] : {}, to);
    Object.keys(from).forEach(v => {
        if (typeof r[v] == 'object' && typeof from[v] == 'object') {
            r[v] = merge(r[v], from[v]);

        } else if (typeof from[v] == 'object') {
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

