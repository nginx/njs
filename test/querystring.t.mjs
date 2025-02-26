/*---
includes: [runTsuite.js, compareObjects.js]
flags: [async]
---*/

import qs from 'querystring';

let escape_tsuite = {
    name: "querystring.escape() tests",
    T: async (params) => {
        let r = qs.escape(params.value);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: { },

    tests: [
        { value: '', expected: '' },
        { value: 'baz=fuz', expected: 'baz%3Dfuz' },
        { value: 'abcαdef', expected: 'abc%CE%B1def' },
]};

let parse_tsuite = {
    name: "querystring.parse() tests",
    T: async (params) => {
        let r;
        let unescape = qs.unescape;

        if (params.unescape) {
            qs.unescape = params.unescape;
        }

        try {
            if (params.options !== undefined) {
                r = qs.parse(params.value, params.sep, params.eq, params.options);

            } else if (params.eq !== undefined) {
                r = qs.parse(params.value, params.sep, params.eq);

            } else if (params.sep !== undefined) {
                r = qs.parse(params.value, params.sep);

            } else {
                r = qs.parse(params.value);
            }

        } finally {
            if (params.unescape) {
                qs.unescape = unescape;
            }
        }

        if (!compareObjects(r, params.expected)) {
            throw Error(`unexpected output "${JSON.stringify(r)}" != "${JSON.stringify(params.expected)}"`);
        }

        return 'SUCCESS';
    },

    opts: { },

    tests: [
        { value: '', expected: {} },
        { value: 'baz=fuz', expected: { baz:'fuz' } },
        { value: 'baz=fuz', expected: { baz:'fuz' } },
        { value: 'baz=fuz&', expected: { baz:'fuz' } },
        { value: '&baz=fuz', expected: { baz:'fuz' } },
        { value: '&&baz=fuz', expected: { baz:'fuz' } },
        { value: 'baz=fuz&muz=tax', expected: { baz:'fuz', muz:'tax' } },
        { value: 'baz=fuz&baz=bar', expected: { baz:['fuz', 'bar'] } },

        { value: qs.encode({ baz:'fuz', muz:'tax' }), expected: { baz:'fuz', muz:'tax' } },

        { value: 'baz=fuz&baz=bar', sep: '&', eq: '=', expected: { baz:['fuz', 'bar'] } },
        { value: 'baz=fuz&baz=bar&baz=zap', expected: { baz:['fuz', 'bar', 'zap'] } },
        { value: 'baz=', expected: { baz:'' } },
        { value: '=fuz', expected: { '':'fuz' } },
        { value: '=fuz=', expected: { '':'fuz=' } },
        { value: '==fu=z', expected: { '':'=fu=z' } },
        { value: '===fu=z&baz=bar', expected: { baz:'bar', '':'==fu=z' } },
        { value: 'freespace', expected: { freespace:'' } },
        { value: 'name&value=12', expected: { name:'', value:'12' } },
        { value: 'baz=fuz&muz=tax', sep: 'fuz', expected: { baz:'', '&muz':'tax' } },
        { value: 'baz=fuz&muz=tax', sep: '', expected: { baz:'fuz', 'muz':'tax' } },
        { value: 'baz=fuz&muz=tax', sep: null, expected: { baz:'fuz', 'muz':'tax' } },
        { value: 'baz=fuz123muz=tax', sep: 123, expected: { baz:'fuz', 'muz':'tax' } },
        { value: 'baz=fuzαααmuz=tax', sep: 'ααα', expected: { baz:'fuz', 'muz':'tax' } },
        { value: 'baz=fuz&muz=tax', sep: '=', expected: { baz:'', 'fuz&muz':'', 'tax':'' } },

        { value: 'baz=fuz&muz=tax', sep: '', eq: '', expected: { baz:'fuz', muz:'tax' } },
        { value: 'baz=fuz&muz=tax', sep: null, eq: 'fuz', expected: { 'baz=':'','muz=tax':'' } },
        { value: 'baz123fuz&muz123tax', sep: null, eq: '123', expected: { baz:'fuz', 'muz':'tax' } },
        { value: 'bazαααfuz&muzαααtax', sep: null, eq: 'ααα', expected: { baz:'fuz', 'muz':'tax' } },

        { value: 'baz=fuz&muz=tax', sep: null, eq: null, options: { maxKeys: 1 }, expected: { baz:'fuz' } },
        { value: 'baz=fuz&muz=tax', sep: null, eq: null, options: { maxKeys: -1 },
          expected: { baz:'fuz', muz:'tax' } },
        { value: 'baz=fuz&muz=tax', sep: null, eq: null, options: { maxKeys: { valueOf: () => { throw 'Oops'; } }},
          exception: 'Oops' },
        { value: 'baz=fuz&muz=tax', sep: null, eq: null,
          options: { decodeURIComponent: (s) => `|${s}|` }, expected: { '|baz|':'|fuz|', '|muz|':'|tax|' } },
        { value: 'baz=fuz&muz=tax', sep: null, eq: null,
          options: { decodeURIComponent: 123 },
          exception: 'TypeError: option decodeURIComponent is not a function' },
        { value: 'baz=fuz&muz=tax', unescape: (s) => `|${s}|`, expected: { '|baz|':'|fuz|', '|muz|':'|tax|' } },
        { value: 'baz=fuz&muz=tax', unescape: 123,
          exception: 'TypeError: QueryString.unescape is not a function' },

        { value: 'ba%32z=f%32uz', expected: { ba2z:'f2uz' } },
        { value: 'ba%F0%9F%92%A9z=f%F0%9F%92%A9uz', expected: { 'ba💩z':'f💩uz' } },
        { value: '==', expected: { '':'=' } },
        { value: 'baz=%F0%9F%A9', expected: { baz:'�' } },
        { value: 'baz=α%00%01%02α', expected: { baz:'α' + String.fromCharCode(0, 1, 2) + 'α' } },
        { value: 'baz=%F6', expected: { baz:'�' } },
        { value: 'baz=%FG', expected: { baz:'%FG' } },
        { value: 'baz=%F', expected: { baz:'%F' } },
        { value: 'baz=%', expected: { baz:'%' } },
        { value: 'ba+z=f+uz', expected: { 'ba z':'f uz' } },
        { value: 'X=' + 'α'.repeat(33), expected: { X:'α'.repeat(33) } },
        { value: 'X=' + 'α1'.repeat(33), expected: { X:'α1'.repeat(33) } },

        { value: {toString: () => { throw 'Oops'; }}, sep: "&", eq: "=",
          exception: 'TypeError: Cannot convert object to primitive value' },
]};

let stringify_tsuite = {
    name: "querystring.stringify() tests",
    T: async (params) => {
        let r;
        let escape = qs.escape;

        if (params.escape) {
            qs.escape = params.escape;
        }

        try {
            if (params.options !== undefined) {
                r = qs.stringify(params.obj, params.sep, params.eq, params.options);

            } else if (params.eq !== undefined) {
                r = qs.stringify(params.obj, params.sep, params.eq);

            } else if (params.sep !== undefined) {
                r = qs.stringify(params.obj, params.sep);

            } else {
                r = qs.stringify(params.obj);
            }

        } finally {
            if (params.escape) {
                qs.escape = escape;
            }
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: { },

    tests: [
        { obj: {}, expected: '' },
        { obj: { baz:'fuz', muz:'tax' }, expected: 'baz=fuz&muz=tax' },
        { obj: { baz:['fuz', 'tax'] }, expected: 'baz=fuz&baz=tax' },
        { obj: {'baαz': 'fαuz', 'muαz': 'tαax' }, expected: 'ba%CE%B1z=f%CE%B1uz&mu%CE%B1z=t%CE%B1ax' },
        { obj: {A:'α', 'δ': 'D' }, expected: 'A=%CE%B1&%CE%B4=D' },
        { obj: { baz:'fuz', muz:'tax' }, sep: '*', expected: 'baz=fuz*muz=tax' },
        { obj: { baz:'fuz', muz:'tax' }, sep: null, eq: '^', expected: 'baz^fuz&muz^tax' },
        { obj: { baz:'fuz', muz:'tax' }, sep: '', eq: '', expected: 'baz=fuz&muz=tax' },
        { obj: { baz:'fuz', muz:'tax' }, sep: '?', eq: '/', expected: 'baz/fuz?muz/tax' },
        { obj: { baz:'fuz', muz:'tax' }, sep: null, eq: null, options: { encodeURIComponent: (key) => `|${key}|` },
          expected: '|baz|=|fuz|&|muz|=|tax|' },
        { obj: { baz:'fuz', muz:'tax' }, sep: null, eq: null, options: { encodeURIComponent: 123 },
          exception: 'TypeError: option encodeURIComponent is not a function' },
        { obj: { baz:'fuz', muz:'tax' }, escape: (key) => `|${key}|`, expected: '|baz|=|fuz|&|muz|=|tax|' },
        { obj: { '':'' }, escape: (s) => s.length == 0 ? '#' : s, expected: '#=#' },
        { obj: { baz:'fuz', muz:'tax' }, escape: 123,
          exception: 'TypeError: QueryString.escape is not a function' },

        { obj: qs.decode('baz=fuz&muz=tax'), expected: 'baz=fuz&muz=tax' },

        { obj: '123', expected: '' },
        { obj: 123, expected: '' },
        { obj: { baz:'fuz' }, expected: 'baz=fuz' },
        { obj: { baz:undefined }, expected: 'baz=' },
        { obj: Object.create({ baz:'fuz' }), expected: '' },
        { obj: [], expected: '' },
        { obj: ['a'], expected: '0=a' },
        { obj: ['a', 'b'], expected: '0=a&1=b' },
        { obj: ['', ''], expected: '0=&1=' },
        { obj: [undefined, null, Symbol(), Object(0), Object('test'), Object(false),,,],
          expected: '0=&1=&2=&3=&4=&5=' },
        { obj: [['a', 'b'], ['c', 'd']], expected: '0=a&0=b&1=c&1=d' },
        { obj: [['a',,,], ['b',,,]], expected: '0=a&0=&0=&1=b&1=&1=' },
        { obj: [[,'a','b',,]], expected: '0=&0=a&0=b&0=' },
]};

let unescape_tsuite = {
    name: "querystring.unescape() tests",
    T: async (params) => {
        let r = qs.unescape(params.value);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: { },

    tests: [
        { value: '', expected: '' },
        { value: 'baz%3Dfuz', expected: 'baz=fuz' },
        { value: 'abc%CE%B1def', expected: 'abcαdef' },
]};

run([
    escape_tsuite,
    parse_tsuite,
    stringify_tsuite,
    unescape_tsuite,
])
.then($DONE, $DONE);
