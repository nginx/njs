/*---
includes: [compatBuffer.js, runTsuite.js, compareArray.js]
flags: [async]
---*/

let alloc_tsuite = {
    name: "Buffer.alloc() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = Buffer.alloc(params.size, params.fill, params.encoding);

        if (r.toString() !== params.expected) {
            throw Error(`unexpected output "${r.toString()}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: { encoding: 'utf-8' },

    tests: [
        { size: 3, fill: 0x61, expected: 'aaa' },
        { size: 3, fill: 'A', expected: 'AAA' },
        { size: 3, fill: 'ABCD', expected: 'ABC' },
        { size: 3, fill: '414243', encoding: 'hex', expected: 'ABC' },
        { size: 4, fill: '414243', encoding: 'hex', expected: 'ABCA' },
        { size: 3, fill: 'QUJD', encoding: 'base64', expected: 'ABC' },
        { size: 3, fill: 'QUJD', encoding: 'base64url', expected: 'ABC' },
        { size: 3, fill: Buffer.from('ABCD'), encoding: 'utf-8', expected: 'ABC' },
        { size: 3, fill: Buffer.from('ABCD'), encoding: 'utf8', expected: 'ABC' },
        { size: 3, fill: 'ABCD', encoding: 'utf-128',
          exception: 'TypeError: "utf-128" encoding is not supported' },
        { size: 3, fill: Buffer.from('def'), expected: 'def' },
    ],
};


let byteLength_tsuite = {
    name: "Buffer.byteLength() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = Buffer.byteLength(params.value, params.encoding);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: { encoding: 'utf-8' },

    tests: [
        { value: 'abc', expected: 3 },
        { value: 'αβγ', expected: 6 },
        { value: 'αβγ', encoding: 'utf-8', expected: 6 },
        { value: 'αβγ', encoding: 'utf8', expected: 6 },
        { value: 'αβγ', encoding: 'utf-128', exception: 'TypeError: "utf-128" encoding is not supported' },
        { value: '414243', encoding: 'hex', expected: 3 },
        { value: 'QUJD', encoding: 'base64', expected: 3 },
        { value: 'QUJD', encoding: 'base64url', expected: 3 },
        { value: Buffer.from('αβγ'), expected: 6 },
        { value: Buffer.alloc(3).buffer, expected: 3 },
        { value: Buffer.from(new Uint8Array([0x60, 0x61, 0x62, 0x63]).buffer, 1), expected: 3 },
    ],
};


let concat_tsuite = {
    name: "Buffer.concat() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r;

        if (params.length) {
            r = Buffer.concat(params.buffers, params.length);

        } else {
            r = Buffer.concat(params.buffers);
        }

        if (r.toString() !== params.expected) {
            throw Error(`unexpected output "${r.toString()}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},
    tests: [
        { buffers: [ Buffer.from('abc'),
                     Buffer.from(new Uint8Array([0x64, 0x65, 0x66]).buffer, 1) ],
          expected: 'abcef' },
        { buffers: [ Buffer.from('abc'), Buffer.from('def'), Buffer.from('') ],
          expected: 'abcdef' },
        { buffers: [ Buffer.from(''), Buffer.from('abc'), Buffer.from('def') ],
          length: 4, expected: 'abcd' },
    ],
};


let compare_tsuite = {
    name: "Buffer.compare() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = Buffer.compare(params.buf1, params.buf2);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf1: Buffer.from('abc'), buf2: Buffer.from('abc'), expected: 0 },
        { buf1: Buffer.from('abc'),
          buf2: Buffer.from(new Uint8Array([0x60, 0x61, 0x62, 0x63]).buffer, 1),
          expected: 0 },
        { buf1: Buffer.from(new Uint8Array([0x60, 0x61, 0x62, 0x63]).buffer, 1),
          buf2: Buffer.from('abc'),
          expected: 0 },
        { buf1: Buffer.from('abc'), buf2: Buffer.from('def'), expected: -1 },
        { buf1: Buffer.from('def'), buf2: Buffer.from('abc'), expected: 1 },
        { buf1: Buffer.from('abc'), buf2: Buffer.from('abcd'), expected: -1 },
        { buf1: Buffer.from('abcd'), buf2: Buffer.from('abc'), expected: 1 },
        { buf1: Buffer.from('abc'), buf2: Buffer.from('ab'), expected: 1 },
        { buf1: Buffer.from('ab'), buf2: Buffer.from('abc'), expected: -1 },
        { buf1: Buffer.from('abc'), buf2: Buffer.from(''), expected: 1 },
        { buf1: Buffer.from(''), buf2: Buffer.from('abc'), expected: -1 },
        { buf1: Buffer.from(''), buf2: Buffer.from(''), expected: 0 },
    ],
};


let comparePrototype_tsuite = {
    name: "buf.compare() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = params.buf.compare(params.target, params.tStart, params.tEnd,
                                   params.sStart, params.sEnd);


        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf: Buffer.from('abc'), target: Buffer.from('abc'), expected: 0 },
        { buf: Buffer.from('abc'),
          target: Buffer.from(new Uint8Array([0x60, 0x61, 0x62, 0x63]).buffer, 1),
          expected: 0 },
        { buf: Buffer.from(new Uint8Array([0x60, 0x61, 0x62, 0x63]).buffer, 1),
          target: Buffer.from('abc'), expected: 0 },
        { buf: Buffer.from('abc'), target: Buffer.from('def'), expected: -1 },
        { buf: Buffer.from('def'), target: Buffer.from('abc'), expected: 1 },
        { buf: Buffer.from('abc'), target: Buffer.from('abcd'), expected: -1 },
        { buf: Buffer.from('abcd'), target: Buffer.from('abc'), expected: 1 },
        { buf: Buffer.from('abc'), target: Buffer.from('ab'), expected: 1 },
        { buf: Buffer.from('ab'), target: Buffer.from('abc'), expected: -1 },
        { buf: Buffer.from('abc'), target: Buffer.from(''), expected: 1 },
        { buf: Buffer.from(''), target: Buffer.from('abc'), expected: -1 },
        { buf: Buffer.from(''), target: Buffer.from(''), expected: 0 },

        { buf: Buffer.from('abcdef'), target: Buffer.from('abc'),
          sEnd: 3, expected: 0 },
        { buf: Buffer.from('abcdef'), target: Buffer.from('def'),
          sStart: 3, expected: 0 },
        { buf: Buffer.from('abcdef'), target: Buffer.from('abc'),
          sStart: 0, sEnd: 3, expected: 0 },
        { buf: Buffer.from('abcdef'), target: Buffer.from('def'),
          sStart: 3, sEnd: 6, expected: 0 },
        { buf: Buffer.from('abcdef'), target: Buffer.from('def'),
          sStart: 3, sEnd: 5, tStart: 0, tEnd: 2, expected: 0 },
    ],
};


let copy_tsuite = {
    name: "buf.copy() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = params.buf.copy(params.target, params.tStart, params.sStart, params.sEnd);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        if (params.target.toString() !== params.expected_buf) {
            throw Error(`unexpected buf "${params.target.toString()}" != "${params.expected_buf}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf: Buffer.from('abcdef'), target: Buffer.from('123456'),
          expected: 6, expected_buf: 'abcdef' },
        { buf: Buffer.from('abcdef'), target: Buffer.from('123456'),
          tStart: 0, expected: 6, expected_buf: 'abcdef' },
        { buf: Buffer.from('abc'), target: Buffer.from('123456789'),
          tStart: 5, expected: 3, expected_buf: '12345abc9' },
        { buf: Buffer.from('abcdef'), target: Buffer.from('123456'),
          tStart: 0, sStart: 0, expected: 6, expected_buf: 'abcdef' },
        { buf: Buffer.from('abcdef'), target: Buffer.from('123456'),
          tStart: 0, sStart: 0, sEnd: 3, expected: 3, expected_buf: 'abc456' },
        { buf: Buffer.from('abcdef'), target: Buffer.from('123456'),
          tStart: 2, sStart: 2, sEnd: 3, expected: 1, expected_buf: '12c456' },
        { buf: Buffer.from('abcdef'), target: Buffer.from('123456'),
          tStart: 7, exception: 'RangeError: \"targetStart\" is out of bounds' },
        { buf: Buffer.from('abcdef'), target: Buffer.from('123456'),
          sStart: 7, exception: 'RangeError: \"sourceStart\" is out of bounds' },
    ],
};


let equals_tsuite = {
    name: "buf.equals() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = params.buf1.equals(params.buf2);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},
    tests: [

        { buf1: Buffer.from('abc'), buf2: Buffer.from('abc'), expected: true },
        { buf1: Buffer.from('abc'),
          buf2: Buffer.from(new Uint8Array([0x60, 0x61, 0x62, 0x63]).buffer, 1),
          expected: true },
        { buf1: Buffer.from(new Uint8Array([0x60, 0x61, 0x62, 0x63]).buffer, 1),
          buf2: Buffer.from('abc'), expected: true },
        { buf1: Buffer.from('abc'), buf2: Buffer.from('def'), expected: false },
        { buf1: Buffer.from('def'), buf2: Buffer.from('abc'), expected: false },
        { buf1: Buffer.from('abc'), buf2: Buffer.from('abcd'), expected: false },
        { buf1: Buffer.from('abcd'), buf2: Buffer.from('abc'), expected: false },
        { buf1: Buffer.from('abc'), buf2: Buffer.from('ab'), expected: false },
        { buf1: Buffer.from('ab'), buf2: Buffer.from('abc'), expected: false },
        { buf1: Buffer.from('abc'), buf2: Buffer.from(''), expected: false },
        { buf1: Buffer.from(''), buf2: Buffer.from('abc'), expected: false },
        { buf1: Buffer.from(''), buf2: Buffer.from(''), expected: true },
    ],
};


let fill_tsuite = {
    name: "buf.fill() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = params.buf.fill(params.value, params.offset, params.end);

        if (r.toString() !== params.expected) {
            throw Error(`unexpected output "${r.toString()}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},
    tests: [
        { buf: Buffer.from('abc'), value: 0x61, expected: 'aaa' },
        { buf: Buffer.from('abc'), value: 0x61, expected: 'aaa', offset: 0, end: 3 },
        { buf: Buffer.from('abc'), value: 0x61, expected: 'abc', offset: 0, end: 0 },
        { buf: Buffer.from('abc'), value: 'A', expected: 'AAA' },
        { buf: Buffer.from('abc'), value: 'ABCD', expected: 'ABC' },
        { buf: Buffer.from('abc'), value: '414243', offset: 'hex', expected: 'ABC' },
        { buf: Buffer.from('abc'), value: '414243', offset: 'utf-128',
          exception: 'TypeError: "utf-128" encoding is not supported' },
        { buf: Buffer.from('abc'), value: 'ABCD', offset: 1, expected: 'aAB' },
        { buf: Buffer.from('abc'), value: Buffer.from('def'), expected: 'def' },
        { buf: Buffer.from('def'),
          value: Buffer.from(new Uint8Array([0x60, 0x61, 0x62, 0x63]).buffer, 1),
          expected: 'abc' },
        { buf: Buffer.from(new Uint8Array([0x60, 0x61, 0x62, 0x63]).buffer, 1),
          value: Buffer.from('def'),
          expected: 'def' },
    ],
};


let from_tsuite = {
    name: "Buffer.from() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let buf = Buffer.from.apply(null, params.args);

        if (params.modify) {
            params.modify(buf);
        }

        if (params.args[0] instanceof ArrayBuffer) {
            if (buf.buffer !== params.args[0]) {
                throw Error(`unexpected buffer "${buf.buffer}" != "${params.args[0]}"`);
            }
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


let includes_tsuite = {
    name: "buf.includes() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = params.buf.includes(params.value, params.offset, params.encoding);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf: Buffer.from('abcdef'), value: 'abc', expected: true },
        { buf: Buffer.from('abcdef'), value: 'def', expected: true },
        { buf: Buffer.from('abcdef'), value: 'abc', offset: 1, expected: false },
        { buf: Buffer.from('abcdef'), value: {},
          exception: 'TypeError: "value" argument must be of type string or an instance of Buffer' },
    ],
};


let indexOf_tsuite = {
    name: "buf.indexOf() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = params.buf.indexOf(params.value, params.offset, params.encoding);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf: Buffer.from('abcdef'), value: 'abc', expected: 0 },
        { buf: Buffer.from('abcdef'), value: 'def', expected: 3 },
        { buf: Buffer.from('abcdef'), value: 'abc', offset: 1, expected: -1 },
        { buf: Buffer.from('abcdef'), value: 'def', offset: 1, expected: 3 },
        { buf: Buffer.from('abcdef'), value: 'def', offset: -3, expected: 3 },
        { buf: Buffer.from('abcdef'), value: 'efgh', offset: 4, expected: -1 },
        { buf: Buffer.from(''), value: '', expected: 0 },
        { buf: Buffer.from(''), value: '', offset: -1, expected: 0 },
        { buf: Buffer.from(''), value: '', offset: 0, expected: 0 },
        { buf: Buffer.from(''), value: '', offset: 1, expected: 0 },
        { buf: Buffer.from('abc'), value: '', offset: -4, expected: 0 },
        { buf: Buffer.from('abc'), value: '', offset: -3, expected: 0 },
        { buf: Buffer.from('abc'), value: '', offset: -2, expected: 1 },
        { buf: Buffer.from('abc'), value: '', offset: -1, expected: 2 },
        { buf: Buffer.from('abc'), value: '', offset: 0, expected: 0 },
        { buf: Buffer.from('abc'), value: '', offset: 1, expected: 1 },
        { buf: Buffer.from('abc'), value: '', offset: 2, expected: 2 },
        { buf: Buffer.from('abc'), value: '', offset: 3, expected: 3 },
        { buf: Buffer.from('abc'), value: '', offset: 4, expected: 3 },
        { buf: Buffer.from('abcdef'), value: '626364', encoding: 'hex', expected: 1 },
        { buf: Buffer.from('abcdef'), value: '626364', encoding: 'utf-128',
          exception: 'TypeError: "utf-128" encoding is not supported' },
        { buf: Buffer.from('abcdef'), value: 0x62, expected: 1 },
        { buf: Buffer.from('abcabc'), value: 0x61, offset: 1, expected: 3 },
        { buf: Buffer.from('abcdef'), value: Buffer.from('def'), expected: 3 },
        { buf: Buffer.from('abcdef'), value: Buffer.from(new Uint8Array([0x60, 0x62, 0x63]).buffer, 1), expected: 1 },
        { buf: Buffer.from('abcdef'), value: {},
          exception: 'TypeError: "value" argument must be of type string or an instance of Buffer' },
    ],
};


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

    opts: {},

    tests: [
        { value: Buffer.from('α'), expected: true },
        { value: new Uint8Array(10), expected: false },
        { value: {}, expected: false },
        { value: 1, expected: false },
]};


let isEncoding_tsuite = {
    name: "Buffer.isEncoding() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = Buffer.isEncoding(params.value);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { value: 'utf-8', expected: true },
        { value: 'utf8', expected: true },
        { value: 'utf-128', expected: false },
        { value: 'hex', expected: true },
        { value: 'base64', expected: true },
        { value: 'base64url', expected: true },
    ],
};


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


let lastIndexOf_tsuite = {
    name: "buf.lastIndexOf() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = params.buf.lastIndexOf(params.value, params.offset, params.encoding);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf: Buffer.from('abcdef'), value: 'abc', expected: 0 },
        { buf: Buffer.from('abcabc'), value: 'abc', expected: 3 },
        { buf: Buffer.from('abcdef'), value: 'def', expected: 3 },
        { buf: Buffer.from('abcdef'), value: 'abc', offset: 1, expected: 0 },
        { buf: Buffer.from('abcdef'), value: 'def', offset: 1, expected: -1 },
        { buf: Buffer.from('xxxABCx'), value: 'ABC', offset: 3, expected: 3 },
        { buf: Buffer.from(''), value: '', expected: 0 },
        { buf: Buffer.from(''), value: '', offset: -1, expected: 0 },
        { buf: Buffer.from(''), value: '', offset: 0, expected: 0 },
        { buf: Buffer.from(''), value: '', offset: 1, expected: 0 },
        { buf: Buffer.from('abc'), value: '', offset: -4, expected: 0 },
        { buf: Buffer.from('abc'), value: '', offset: -3, expected: 0 },
        { buf: Buffer.from('abc'), value: '', offset: -2, expected: 1 },
        { buf: Buffer.from('abc'), value: '', offset: -1, expected: 2 },
        { buf: Buffer.from('abc'), value: '', offset: 0, expected: 0 },
        { buf: Buffer.from('abc'), value: '', offset: 1, expected: 1 },
        { buf: Buffer.from('abc'), value: '', offset: 2, expected: 2 },
        { buf: Buffer.from('abc'), value: '', offset: 3, expected: 3 },
        { buf: Buffer.from('abc'), value: '', offset: 4, expected: 3 },
        { buf: Buffer.from(Buffer.alloc(7).fill('Zabcdef').buffer, 1), value: 'abcdef', expected: 0 },
        { buf: Buffer.from(Buffer.alloc(7).fill('Zabcdef').buffer, 1), value: 'abcdefg', expected: -1 },
        { buf: Buffer.from('abcdef'), value: '626364', encoding: 'hex', expected: 1 },
        { buf: Buffer.from('abcdef'), value: '626364', encoding: 'utf-128',
          exception: 'TypeError: "utf-128" encoding is not supported' },
        { buf: Buffer.from('abcabc'), value: 0x61, expected: 3 },
        { buf: Buffer.from('abcabc'), value: 0x61, offset: 1, expected: 0 },
        { buf: Buffer.from('ab'), value: 7, offset: 2, expected: -1 },
        { buf: Buffer.from('abcdef'), value: Buffer.from('def'), expected: 3 },
        { buf: Buffer.from('abcdef'), value: Buffer.from(new Uint8Array([0x60, 0x62, 0x63]).buffer, 1), expected: 1 },
        { buf: Buffer.from('abcdef'), value: {},
          exception: 'TypeError: "value" argument must be of type string or an instance of Buffer' },
    ],
};


let readXIntXX_tsuite = {
    name: "buf.readXIntXX() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let b = params.buf;
        let r = [
                  b.readInt8(params.offset),
                  b.readUInt8(params.offset),
                  b.readInt16LE(params.offset),
                  b.readInt16BE(params.offset),
                  b.readUInt16LE(params.offset),
                  b.readUInt16BE(params.offset),
                  b.readInt32LE(params.offset),
                  b.readInt32BE(params.offset),
                  b.readUInt32LE(params.offset),
                  b.readUInt32BE(params.offset),
                ];


        if (!compareArray(r, params.expected)) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 0,
          expected: [ -86,170,-17494,-21829,48042,43707,-573785174,-1430532899,3721182122,2864434397 ] },
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 1,
          expected: [ -69,187,-13125,-17460,52411,48076,-287454021,-1144201746,4007513275,3150765550 ] },
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 2,
          expected: [ -52,204,-8756,-13091,56780,52445,-1122868,-857870593,4293844428,3437096703 ] },
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 3,
          exception: 'RangeError: Index out of range' },
    ],
};


let readFloat_tsuite = {
    name: "buf.readFloat() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let b = Buffer.alloc(9);
        let r = b.writeFloatLE(123.125, 0);
        if (r !== 4) {
            throw Error(`unexpected output "${r}" != "4"`);
        }

        if (b.readFloatLE(0) !== 123.125) {
            throw Error(`unexpected output "${b.readFloatLE(0)}" != "123.125"`);
        }

        r = b.writeFloatBE(123.125, 0);
        if (r !== 4) {
            throw Error(`unexpected output "${r}" != "4"`);
        }

        if (b.readFloatBE(0) !== 123.125) {
            throw Error(`unexpected output "${b.readFloatBE(0)}" != "123.125"`);
        }

        r = b.writeDoubleLE(123.125, 1);
        if (r !== 9) {
            throw Error(`unexpected output "${r}" != "9"`);
        }

        if (b.readDoubleLE(1) !== 123.125) {
            throw Error(`unexpected output "${b.readDoubleLE(1)}" != "123.125"`);
        }

        r = b.writeDoubleBE(123.125, 1);
        if (r !== 9) {
            throw Error(`unexpected output "${r}" != "9"`);
        }

        if (b.readDoubleBE(1) !== 123.125) {
            throw Error(`unexpected output "${b.readDoubleBE(1)}" != "123.125"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        {}
    ],
};


let readGeneric_tsuite = {
    name: "buf.readGeneric() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let b = params.buf;
        let r = [
                  b.readUIntLE(params.offset, params.length),
                  b.readUIntBE(params.offset, params.length),
                  b.readIntLE(params.offset, params.length),
                  b.readIntBE(params.offset, params.length),
                ];


        if (!compareArray(r, params.expected)) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 0, length: 1,
          expected: [ 170, 170, -86, -86 ] },
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 1, length: 2,
          expected: [ 52411,48076,-13125,-17460 ] },
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 2, length: 3,
          expected: [ 15654348,13426158,-1122868,-3351058 ] },
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 3, length: 4,
          exception: 'RangeError: Index out of range' },
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 0, length: 0,
          exception: 'RangeError: byteLength must be <= 6' },
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 0, length: 5,
          expected: [ 1025923398570,733295205870,-73588229206,-366216421906 ] },
        { buf: Buffer.from([0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff]), offset: 0, length: 6,
          expected: [ 281401388481450,187723572702975,-73588229206,-93751404007681 ] },
    ],
};


let slice_tsuite = {
    name: "buf.slice() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = params.buf.slice(params.start, params.end);

        if (r.toString() !== params.expected) {
            throw Error(`unexpected output "${r.toString()}" != "${params.expected}"`);
        }

        params.buf[2] = 0x5a;

        if (r.constructor.name !== 'Buffer' || r.__proto__ !== params.buf.__proto__) {
            throw Error(`unexpected output "${r.constructor.name}" != "Buffer"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf: Buffer.from('abcdef'), start: 1, expected: 'bcdef' },
        { buf: Buffer.from('abcdef'), start: 1, end: 3, expected: 'bc' },
    ],
};


let subarray_tsuite = {
    name: "buf.subarray() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = params.buf.subarray(params.start, params.end);

        params.buf[0] = 0x5a;

        if (r.toString() !== params.expected) {
            throw Error(`unexpected output "${r.toString()}" != "${params.expected}"`);
        }

        if (r.constructor.name !== 'Buffer' || r.__proto__ !== params.buf.__proto__) {
            throw Error(`unexpected output "${r.constructor.name}" != "Buffer"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { buf: Buffer.from('abcdef'), start: 0, end: 3, expected: 'Zbc' },
        { buf: Buffer.from('abcdef'), start: 1, expected: 'bcdef' },
    ],
};


let swap_tsuite = {
    name: "buf.swap() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let r = Buffer.from(params.value, 'hex')[params.swap]();

        if (r.toString('hex') !== params.expected) {
            throw Error(`unexpected output "${r.toString('hex')}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: { swap: 'swap16' },

    tests: [
        { value: '01020304', expected: '02010403' },
        { value: '010203', exception: 'RangeError: Buffer size must be a multiple of 2' },
        { value: 'aabbccddeeff0011', swap: 'swap32', expected: 'ddccbbaa1100ffee' },
        { value: 'aabbcc', swap: 'swap32', exception: 'RangeError: Buffer size must be a multiple of 4' },
        { value: 'aabbccddeeff00112233445566778899', swap: 'swap64', expected: '1100ffeeddccbbaa9988776655443322' },
    ],
};


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

    opts: { fmt: 'utf-8' },

    tests: [
        { value: '💩', expected: '💩' },
        { value: String.fromCharCode(0xD83D, 0xDCA9), expected: '💩' },
        { value: String.fromCharCode(0xD83D, 0xDCA9), expected: String.fromCharCode(0xD83D, 0xDCA9) },
        { value: new Uint8Array([0xff, 0xde, 0xba]), fmt: "hex", expected: 'ffdeba' },
        { value: new Uint8Array([0xff, 0xde, 0xba]), fmt: "base64", expected: '/966' },
        { value: new Uint8Array([0xff, 0xde, 0xba]), fmt: "base64url", expected: '_966' },
        { value: "ABCD", fmt: "base64", expected: 'QUJDRA==' },
        { value: "ABCD", fmt: "base64url", expected: 'QUJDRA' },
        { value: '', fmt: "utf-128", exception: 'TypeError: "utf-128" encoding is not supported' },
]};


let write_tsuite = {
    name: "buf.write() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let b = Buffer.alloc(10).fill('Z');
        let r;

        if (typeof params.offset != 'undefined' && typeof params.length != 'undefined') {
            r = b.write(params.value, params.offset, params.length, params.encoding);

        } else if (typeof params.offset != 'undefined') {
            r = b.write(params.value, params.offset, params.encoding);

        } else {
            r = b.write(params.value, params.encoding);
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        if (b.toString() !== params.expected_buf) {
            throw Error(`unexpected output "${b.toString()}" != "${params.expected_buf}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { value: 'abc', expected: 3, expected_buf: 'abcZZZZZZZ' },
        { value: 'abc', offset: 1, expected: 3, expected_buf: 'ZabcZZZZZZ' },
        { value: 'abc', offset: 1, length: 2, expected: 2, expected_buf: 'ZabZZZZZZZ' },
        { value: 'αβγ', offset: 1, expected: 6, expected_buf: 'ZαβγZZZ' },
        { value: 'αβγ', offset: 1, length: 1, expected: 0, expected_buf: 'ZZZZZZZZZZ' },
        { value: 'αβγ', offset: 1, length: 2, expected: 2, expected_buf: 'ZαZZZZZZZ' },
        { value: '414243', encoding: 'hex', expected: 3, expected_buf: 'ABCZZZZZZZ' },
        { value: '414243', encoding: 'hex', offset: 8, expected: 2, expected_buf: 'ZZZZZZZZAB' },
        { value: "x".repeat(12), expected: 10, expected_buf: 'xxxxxxxxxx' },
        { value: "x".repeat(12), offset: 1, expected: 9, expected_buf: 'Zxxxxxxxxx' },
        { value: "x", offset: 1, length: 2, encoding: 'utf-128',
          exception: 'TypeError: "utf-128" encoding is not supported' },
        { value: "x".repeat(10), offset: 10, expected: 0, expected_buf: 'ZZZZZZZZZZ' },
        { value: "x".repeat(10), offset: 11, exception: 'RangeError: Index out of range' },
    ],
};


let writeXIntXX_tsuite = {
    name: "buf.writeXIntXX() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let b = Buffer.alloc(10).fill('Z');
        let r = b[params.write](params.value, params.offset);

        if (params.exception) {
            throw Error(`expected exception "${params.exception}"`);
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        if (b.toString('hex') !== params.expected_buf) {
            throw Error(`unexpected output "${b.toString('hex')}" != "${params.expected_buf}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { write: 'writeInt8', value: 0xaa, exception: 'RangeError: Index out of range' },
        { write: 'writeInt8', value: 0x00, offset: 3, expected: 4, expected_buf: '5a5a5a005a5a5a5a5a5a' },
        { write: 'writeUInt8', value: 0xaa, offset: 0, expected: 1, expected_buf: 'aa5a5a5a5a5a5a5a5a5a' },
        { write: 'writeInt16LE', value: 0xaabb, exception: 'RangeError: Index out of range' },
        { write: 'writeInt16BE', value: 0x7788, offset: 1, expected: 3, expected_buf: '5a77885a5a5a5a5a5a5a' },
        { write: 'writeUInt16LE', value: 0xaabb, offset: 0, expected: 2, expected_buf: 'bbaa5a5a5a5a5a5a5a5a' },
        { write: 'writeUInt16BE', value: 0x7788, offset: 1, expected: 3, expected_buf: '5a77885a5a5a5a5a5a5a' },
        { write: 'writeInt32LE', value: 0xaabbccdd, exception: 'RangeError: Index out of range' },
        { write: 'writeInt32BE', value: 0x778899aa, offset: 1, expected: 5, expected_buf: '5a778899aa5a5a5a5a5a' },
        { write: 'writeUInt32LE', value: 0xaabbccdd, offset: 0, expected: 4, expected_buf: 'ddccbbaa5a5a5a5a5a5a' },
        { write: 'writeUInt32BE', value: 0x778899aa, offset: 1, expected: 5, expected_buf: '5a778899aa5a5a5a5a5a' },
    ],
};


let writeGeneric_tsuite = {
    name: "buf.writeGeneric() tests",
    skip: () => (!has_buffer()),
    T: async (params) => {
        let b = Buffer.alloc(10).fill('Z');
        let r = b[params.write](params.value, params.offset, params.length);

        if (params.exception) {
            throw Error(`expected exception "${params.exception}"`);
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        if (b.toString('hex') !== params.expected_buf) {
            throw Error(`unexpected output "${b.toString('hex')}" != "${params.expected_buf}"`);
        }

        return 'SUCCESS';
    },

    opts: {},

    tests: [
        { write: 'writeUIntLE', value: 0xaa, length: 1,
          exception: 'RangeError: Index out of range' },
        { write: 'writeUIntLE', value: 0x44, length: 1, offset: 3,
          expected: 4, expected_buf: '5a5a5a445a5a5a5a5a5a' },
        { write: 'writeUIntBE', value: 0xaabb, length: 2, offset: 0,
          expected: 2, expected_buf: 'aabb5a5a5a5a5a5a5a5a' },
        { write: 'writeUIntBE', value: 0x7788, length: 2, offset: 1,
          expected: 3, expected_buf: '5a77885a5a5a5a5a5a5a' },
        { write: 'writeIntLE', value: 0x445566, length: 3, offset: 5,
          expected: 8, expected_buf: '5a5a5a5a5a6655445a5a' },
        { write: 'writeIntBE', value: 0x778899, length: 3, offset: 1,
          expected: 4, expected_buf: '5a7788995a5a5a5a5a5a' },
        { write: 'writeIntLE', value: 0x44556677, length: 4, offset: 5,
          expected: 9, expected_buf: '5a5a5a5a5a776655445a' },
        { write: 'writeIntBE', value: 0xaabbccdd, length: 4, offset: 1,
          exception: 'RangeError: Index out of range' },
        { write: 'writeUIntLE', value: 0xaabbccddee, length: 5, offset: 0,
          expected: 5, expected_buf: 'eeddccbbaa5a5a5a5a5a' },
        { write: 'writeUIntBE', value: 0x778899aabbcc, length: 6, offset: 1,
          expected: 7, expected_buf: '5a778899aabbcc5a5a5a' },
        { write: 'writeUIntBE', value: 0, length: 7,
          exception: 'The value of "byteLength" is out of range' },

    ],
};


run([
    alloc_tsuite,
    byteLength_tsuite,
    concat_tsuite,
    compare_tsuite,
    comparePrototype_tsuite,
    copy_tsuite,
    equals_tsuite,
    fill_tsuite,
    from_tsuite,
    includes_tsuite,
    indexOf_tsuite,
    isBuffer_tsuite,
    isEncoding_tsuite,
    lastIndexOf_tsuite,
    readXIntXX_tsuite,
    readFloat_tsuite,
    readGeneric_tsuite,
    slice_tsuite,
    subarray_tsuite,
    swap_tsuite,
    toJSON_tsuite,
    toString_tsuite,
    write_tsuite,
    writeXIntXX_tsuite,
    writeGeneric_tsuite,
])
.then($DONE, $DONE);
