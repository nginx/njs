/*---
includes: [compatBuffer.js, runTsuite.js]
flags: [async]
---*/

function p(args, default_opts) {
    let params = merge({}, default_opts);
    params = merge(params, args);

    return params;
}

let from_tsuite = {
    name: "Buffer.from() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let buf = Buffer.from.apply(null, params.args);

        if (params.modify) {
            params.modify(buf);
        }

        let r = buf.toString(params.fmt);

        if (r.length !== params.expected.length) {
            throw Error(`unexpected "${r}" length ${r.length} != ${params.expected.length}`);
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },
    prepare_args: p,
    opts: { fmt: 'utf-8' },

    tests: [
        { args: [[0x62, 0x75, 0x66, 0x66, 0x65, 0x72]], expected: 'buffer' },
        { args: [{length:3, 0:0x62, 1:0x75, 2:0x66}], expected: 'buf' },
        { args: [[-1, 1, 255, 22323, -Infinity, Infinity, NaN]], fmt: "hex", expected: 'ff01ff33000000' },
        { args: [{length:5, 0:'A'.charCodeAt(0), 2:'X', 3:NaN, 4:0xfd}], fmt: "hex", expected: '41000000fd' },
        { args: [[1, 2, 0.23, '5', 'A']], fmt: "hex", expected: '0102000500' },
        { args: [new Uint8Array([0xff, 0xde, 0xba])], fmt: "hex", expected: 'ffdeba' },

        { args: [(new Uint8Array([0xaa, 0xbb, 0xcc])).buffer], fmt: "hex", expected: 'aabbcc' },
        { args: [(new Uint8Array([0xaa, 0xbb, 0xcc])).buffer, 1], fmt: "hex", expected: 'bbcc' },
        { args: [(new Uint8Array([0xaa, 0xbb, 0xcc])).buffer, 1, 1], fmt: "hex", expected: 'bb' },
        { args: [(new Uint8Array([0xaa, 0xbb, 0xcc])).buffer, '1', '1'], fmt: "hex", expected: 'bb' },
        { args: [(new Uint8Array([0xaa, 0xbb, 0xcc])).buffer, 1, 0], fmt: "hex", expected: '' },
        { args: [(new Uint8Array([0xaa, 0xbb, 0xcc])).buffer, 1, -1], fmt: "hex", expected: '' },

        { args: [(new Uint8Array([0xaa, 0xbb, 0xcc])).buffer], fmt: "hex",
          modify: (buf) => { buf[1] = 0; },
          expected: 'aa00cc' },

        { args: [new Uint16Array([234, 123])], fmt: "hex", expected: 'ea7b' },
        { args: [new Uint32Array([234, 123])], fmt: "hex", expected: 'ea7b' },
        { args: [new Float32Array([234.001, 123.11])], fmt: "hex", expected: 'ea7b' },
        { args: [new Uint32Array([234, 123])], fmt: "hex", expected: 'ea7b' },
        { args: [new Float64Array([234.001, 123.11])], fmt: "hex", expected: 'ea7b' },

        { args: [(new Uint8Array(2)).buffer, -1],
          exception: 'RangeError: invalid index' },
        { args: [(new Uint8Array(2)).buffer, 3],
          exception: 'RangeError: \"offset\" is outside of buffer bounds' },
        { args: [(new Uint8Array(2)).buffer, 1, 2],
          exception: 'RangeError: \"length\" is outside of buffer bounds' },

        { args: [Buffer.from([0xaa, 0xbb, 0xcc]).toJSON()], fmt: "hex", expected: 'aabbcc' },
        { args: [{type: 'Buffer', data: [0xaa, 0xbb, 0xcc]}], fmt: "hex", expected: 'aabbcc' },
        { args: [new String('00aabbcc'), 'hex'], fmt: "hex", expected: '00aabbcc' },
        { args: [Buffer.from([0xaa, 0xbb, 0xcc]).toJSON()], fmt: "hex", expected: 'aabbcc' },

        { args: [(function() {var arr = new Array(1, 2, 3); arr.valueOf = () => arr; return arr})()],
          fmt: "hex", expected: '010203' },
        { args: [(function() {var obj = new Object(); obj.valueOf = () => obj; return obj})()],
          exception: 'TypeError: first argument is not a string or Buffer-like object' },
        { args: [(function() {var obj = new Object(); obj.valueOf = () => undefined; return obj})()],
          exception: 'TypeError: first argument is not a string or Buffer-like object' },
        { args: [(function() {var obj = new Object(); obj.valueOf = () => null; return obj})()],
          exception: 'TypeError: first argument is not a string or Buffer-like object' },
        { args: [(function() {var obj = new Object(); obj.valueOf = () => new Array(1, 2, 3); return obj})()],
          fmt: "hex", expected: '010203' },
        { args: [(function() {var a = [1,2,3,4]; a[1] = { valueOf() { a.length = 3; return 1; } }; return a})()],
          fmt: "hex", expected: '01010300' },

        { args: [{type: 'B'}],
          exception: 'TypeError: first argument is not a string or Buffer-like object' },
        { args: [{type: undefined}],
          exception: 'TypeError: first argument is not a string or Buffer-like object' },
        { args: [{type: 'Buffer'}],
          exception: 'TypeError: first argument is not a string or Buffer-like object' },
        { args: [{type: 'Buffer', data: null}],
          exception: 'TypeError: first argument is not a string or Buffer-like object' },
        { args: [{type: 'Buffer', data: {}}],
          exception: 'TypeError: first argument is not a string or Buffer-like object' },

        { args: ['', 'utf-128'], exception: 'TypeError: "utf-128" encoding is not supported' },

        { args: [''], fmt: "hex", expected: '' },
        { args: ['α'], fmt: "hex", expected: 'ceb1' },
        { args: ['α', 'utf-8'], fmt: "hex", expected: 'ceb1' },
        { args: ['α', 'utf8'], fmt: "hex", expected: 'ceb1' },
        { args: ['', 'hex'], fmt: "hex", expected: '' },
        { args: ['aa0', 'hex'], fmt: "hex", expected: 'aa' },
        { args: ['00aabbcc', 'hex'], fmt: "hex", expected: '00aabbcc' },
        { args: ['deadBEEF##', 'hex'], fmt: "hex", expected: 'deadbeef' },
        { args: ['6576696c', 'hex'], expected: 'evil' },
        { args: ['f3', 'hex'], expected: '�' },

        { args: ['', "base64"], expected: '' },
        { args: ['#', "base64"], expected: '' },
        { args: ['Q', "base64"], expected: '' },
        { args: ['QQ', "base64"], expected: 'A' },
        { args: ['QQ=', "base64"], expected: 'A' },
        { args: ['QQ==', "base64"], expected: 'A' },
        { args: ['QUI=', "base64"], expected: 'AB' },
        { args: ['QUI', "base64"], expected: 'AB' },
        { args: ['QUJD', "base64"], expected: 'ABC' },
        { args: ['QUJDRA==', "base64"], expected: 'ABCD' },

        { args: ['', "base64url"], expected: '' },
        { args: ['QQ', "base64url"], expected: 'A' },
        { args: ['QUI', "base64url"], expected: 'AB' },
        { args: ['QUJD', "base64url"], expected: 'ABC' },
        { args: ['QUJDRA', "base64url"], expected: 'ABCD' },
        { args: ['QUJDRA#', "base64url"], expected: 'ABCD' },
]};

let isBuffer_tsuite = {
    name: "Buffer.isBuffer() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = Buffer.isBuffer(params.value);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },
    prepare_args: p,
    opts: {},

    tests: [
        { value: Buffer.from('α'), expected: true },
        { value: new Uint8Array(10), expected: false },
        { value: {}, expected: false },
        { value: 1, expected: false },
]};


function compare_object(a, b) {
    if (a === b) {
        return true;
    }

    if (typeof a !== 'object' || typeof b !== 'object') {
        return false;
    }

    if (Object.keys(a).length !== Object.keys(b).length) {
        return false;
    }

    for (let key in a) {
        if (!compare_object(a[key], b[key])) {
            return false;
        }

    }

    return true;
}


let toJSON_tsuite = {
    name: "Buffer.toJSON() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = Buffer.from(params.value).toJSON();

        if (!compare_object(r, params.expected)) {
            throw Error(`unexpected output "${JSON.stringify(r)}" != "${JSON.stringify(params.expected)}"`);
        }

        return 'SUCCESS';
    },

    prepare_args: p,
    opts: {},
    tests: [
        { value: '', expected: { type: 'Buffer', data: [] } },
        { value: 'αβγ', expected: { type: 'Buffer', data: [0xCE, 0xB1, 0xCE, 0xB2, 0xCE, 0xB3] } },
        { value: new Uint8Array([0xff, 0xde, 0xba]), expected: { type: 'Buffer', data: [0xFF, 0xDE, 0xBA] } },
    ],
};


let toString_tsuite = {
    name: "Buffer.toString() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = Buffer.from(params.value).toString(params.fmt);

        if (r.length !== params.expected.length) {
            throw Error(`unexpected "${r}" length ${r.length} != ${params.expected.length}`);
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },
    prepare_args: p,
    opts: { fmt: 'utf-8' },

    tests: [
        { value: '💩', expected: '💩' },
        { value: String.fromCharCode(0xD83D, 0xDCA9), expected: '💩' },
        { value: String.fromCharCode(0xD83D, 0xDCA9), expected: String.fromCharCode(0xD83D, 0xDCA9) },
        { value: new Uint8Array([0xff, 0xde, 0xba]), fmt: "hex", expected: 'ffdeba' },
        { value: new Uint8Array([0xff, 0xde, 0xba]), fmt: "base64", expected: '/966' },
        { value: new Uint8Array([0xff, 0xde, 0xba]), fmt: "base64url", expected: '_966' },
        { value: '', fmt: "utf-128", exception: 'TypeError: "utf-128" encoding is not supported' },
]};

run([
    from_tsuite,
    isBuffer_tsuite,
    toJSON_tsuite,
    toString_tsuite,
])
.then($DONE, $DONE);
