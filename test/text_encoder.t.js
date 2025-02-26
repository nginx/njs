
/*---
includes: [runTsuite.js, compareArray.js]
flags: [async]
---*/

let encode_tsuite = {
    name: "TextEncoder() encode tests",
    T: async (params) => {
        let te = new TextEncoder();

        if (te.encoding !== 'utf-8') {
            throw Error(`unexpected encoding "${td.encoding}" != "utf-8"`);
        }

        let res = te.encode(params.value);

        if (!(res instanceof Uint8Array)) {
            throw Error(`unexpected result "${res}" is not Uint8Array`);
        }

        if (!compareArray(Array.from(res), params.expected)) {
            throw Error(`unexpected output "${res}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { value: "", expected: [] },
        { value: "abc", expected: [97, 98, 99] },
        { value: "α1α", expected: [206, 177, 49, 206, 177] },
        { value: 0.12, exception: 'TypeError: TextEncoder.prototype.encode requires a string' },
    ],
};

let encodeinto_tsuite = {
    name: "TextEncoder() encodeInto tests",
    T: async (params) => {
        let te = new TextEncoder();

        let res = te.encodeInto(params.value, params.dest);

        if (res.written !== params.expected.length) {
            throw Error(`unexpected written "${res.written}" != "${params.expected.length}"`);
        }

        if (res.read !== params.read) {
            throw Error(`unexpected read "${res.read}" != "${params.read}"`);
        }

        if (!compareArray(Array.from(params.dest).slice(0, res.written), params.expected)) {
            throw Error(`unexpected output "${res}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { value: "", dest: new Uint8Array(4), expected: [], read: 0 },
        { value: "aα", dest: new Uint8Array(3), expected: [97, 206, 177], read: 2 },
        { value: "αααα", dest: new Uint8Array(4), expected: [206, 177, 206, 177], read: 2 },
        { value: "αααα", dest: new Uint8Array(5), expected: [206, 177, 206, 177], read: 2 },
        { value: "αααα", dest: new Uint8Array(6), expected: [206, 177, 206, 177, 206, 177], read: 3 },
        { value: "", dest: 0.12, exception: 'TypeError: TextEncoder.prototype.encodeInto requires a string' },
        { value: 0.12, exception: 'TypeError: TextEncoder.prototype.encodeInto requires a string' },
    ],
};

run([
    encode_tsuite,
    encodeinto_tsuite,
])
.then($DONE, $DONE);
