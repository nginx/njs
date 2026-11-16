/*---
includes: [runTsuite.js, compareArray.js]
flags: [async]
---*/

function p(args, default_opts) {
    let params = merge({}, default_opts);
    params = merge(params, args);

    return params;
}

let stream_tsuite = {
    name: "TextDecoder() stream tests",
    T: async (params) => {
        let td = new TextDecoder('utf-8');

        if (td.encoding !== 'utf-8') {
            throw Error(`unexpected encoding "${td.encoding}" != "utf-8"`);
        }

        if (td.fatal !== false) {
            throw Error(`unexpected fatal "${td.fatal}" != "false"`);
        }

        if (td.ignoreBOM !== false) {
            throw Error(`unexpected ignoreBOM "${td.ignoreBOM}" != "false"`);
        }

        let chunks = [];
        for (var i = 0; i < params.chunks.length; i++) {
            let r = td.decode(params.chunks[i], { stream: (i != params.chunks.length - 1) });
            chunks.push(r);
        }

        if (!compareArray(chunks, params.expected)) {
            throw Error(`unexpected output "${chunks.join('|')}" != "${params.expected.join('|')}"`);
        }

        return 'SUCCESS';
    },

    tests: [
        { chunks: [new Uint8Array([0xF0, 0x9F, 0x8C, 0x9F])],
          expected: ['🌟'] },
        // BOM is ignored
        { chunks: [new Uint8Array([0xEF, 0xBB, 0xBF, 0xF0, 0x9F, 0x8C, 0x9F])],
          expected: ['🌟'] },
        { chunks: [(new Uint8Array([0xF0, 0x9F, 0x8C, 0x9F])).buffer],
          expected: ['🌟'] },
        { chunks: [new Uint32Array((new Uint8Array([0xF0, 0x9F, 0x8C, 0x9F])).buffer)],
          expected: ['🌟'] },
        { chunks: [new Uint8Array((new Uint8Array([0x00, 0xF0, 0x9F, 0x8C, 0x9F, 0x00])).buffer, 1, 4)],
          expected: ['🌟'] },
        { chunks: [new Uint8Array([0xF0, 0x9F]), new Uint8Array([0x8C, 0x9F])],
          expected: ['', '🌟'] },
        { chunks: [new Uint8Array([0xF0, 0xA0]), new Uint8Array([0xAE]), new Uint8Array([0xB7])],
          expected: ['', '', '𠮷'] },
        { chunks: [new Uint8Array([0xF0, 0xA0]), new Uint8Array([])],
          expected: ['', '�'] },
        // BOM is ignored only at the start of the stream
        { chunks: [new Uint8Array([0x41]),
                   new Uint8Array([0xEF, 0xBB, 0xBF, 0x42])],
          expected: ['A', '\uFEFFB'] },
        { chunks: [new Uint8Array([0xEF, 0xBB, 0xBF]),
                   new Uint8Array([0xEF, 0xBB, 0xBF, 0x42])],
          expected: ['', '\uFEFFB'] },
        { chunks: [new Uint8Array([0xEF]), new Uint8Array([0xBB, 0xBF, 0x43])],
          expected: ['', 'C'] },
        { chunks: [new Uint8Array([0xEF, 0xBB]), new Uint8Array([0xBF]),
                   new Uint8Array([0x44])],
          expected: ['', '', 'D'] },
        { chunks: [new Uint8Array([0xEF, 0xBB]), new Uint8Array([0x45])],
          expected: ['', '�E'] },
        { chunks: [''],
          exception: 'TypeError: TypeError: not a TypedArray' },
    ],
};

let fatal_tsuite = {
    name: "TextDecoder() fatal tests",
    T: async (params) => {
        let td = new TextDecoder('utf8', {fatal: true, ignoreBOM: true});

        if (td.encoding !== 'utf-8') {
            throw Error(`unexpected encoding "${td.encoding}" != "utf-8"`);
        }

        if (td.fatal !== true) {
            throw Error(`unexpected fatal "${td.fatal}" != "true"`);
        }

        if (td.ignoreBOM !== true) {
            throw Error(`unexpected ignoreBOM "${td.ignoreBOM}" != "true"`);
        }

        let chunks = [];
        for (var i = 0; i < params.chunks.length; i++) {
            let r = td.decode(params.chunks[i]);
            chunks.push(r);
        }

        if (!compareArray(chunks, params.expected)) {
            throw Error(`unexpected output "${chunks.join('|')}" != "${params.expected.join('|')}"`);
        }

        return 'SUCCESS';
    },

    tests: [
        { chunks: [new Uint8Array([0xF0, 0xA0, 0xAE, 0xB7])],
          expected: ['𠮷'] },
        { chunks: [new Uint8Array([0xF0, 0xA0, 0xAE])],
          exception: 'Error: The encoded data was not valid' },
        { chunks: [new Uint8Array([0xF0, 0xA0])],
          exception: 'Error: The encoded data was not valid' },
        { chunks: [new Uint8Array([0xF0])],
          exception: 'Error: The encoded data was not valid' },
    ],
};

let reuse_tsuite = {
    name: "TextDecoder() reuse tests",
    T: async (params) => {
        let td = new TextDecoder('utf-8', params.opts || {fatal: true});
        let chunks = [];

        for (let i = 0; i < params.chunks.length; i++) {
            try {
                chunks.push(td.decode(new Uint8Array(params.chunks[i]),
                                      {stream: params.stream[i]}));
            } catch (e) {
                if (!e.toString().startsWith('TypeError:')) {
                    throw e;
                }

                chunks.push(null);
            }
        }

        if (!compareArray(chunks, params.expected)) {
            throw Error(`unexpected output ${JSON.stringify(chunks)}`);
        }

        return 'SUCCESS';
    },

    tests: [
        { chunks: [[0xFF], [0xEF, 0xBB, 0xBF, 0x41]],
          stream: [false, false], expected: [null, 'A'] },
        { chunks: [[0xFF], [0xEF, 0xBB, 0xBF, 0x41]],
          stream: [true, false], expected: [null, 'A'] },
        { chunks: [[0x41, 0xFF], [0xEF, 0xBB, 0xBF, 0x41]],
          stream: [true, false], expected: [null, 'A'] },
        { chunks: [[0xEF, 0xBB, 0xBF, 0xFF], [0xEF, 0xBB, 0xBF, 0x41]],
          stream: [true, false], expected: [null, 'A'] },
        { chunks: [[0xEF], [0xBB, 0xBF, 0xFF], [0xEF, 0xBB, 0xBF, 0x41]],
          stream: [true, true, false], expected: ['', null, 'A'] },
        { chunks: [[0x41], [0xFF], [0xEF, 0xBB, 0xBF, 0x42]],
          stream: [true, false, false], expected: ['A', null, 'B'] },
        { chunks: [[0x41], [0xFF], [0xEF, 0xBB, 0xBF, 0x42]],
          stream: [true, true, false], expected: ['A', null, '\uFEFFB'] },
        { chunks: [[0xEF, 0xBB], [], [0xEF, 0xBB, 0xBF, 0x41]],
          stream: [true, false, false], expected: ['', null, 'A'] },
        { chunks: [[0xEF, 0xBB, 0xBF, 0x41], [0xEF, 0xBB, 0xBF, 0x42]],
          stream: [false, false], expected: ['A', 'B'] },
        { chunks: [[], [0xEF], [], [0xBB], [0xBF], []],
          stream: [true, true, true, true, true, false],
          expected: ['', '', '', '', '', ''] },
        { chunks: [[0xEF], [0xBB], [0xBF]], opts: {ignoreBOM: true},
          stream: [true, true, false], expected: ['', '', '\uFEFF'] },
    ],
};

let args_tsuite = {
    name: "TextDecoder() optional arguments tests",
    T: async (params) => {
        let td = new TextDecoder('utf-8');
        let r = params.prefix ? td.decode(params.prefix, {stream: true}) : '';

        r += td.decode.apply(td, params.args);

        if (params.exception) {
            return `expected ${params.exception} not thrown`;
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    tests: [
        { args: [], expected: '' },
        { args: [undefined], expected: '' },
        { args: [new Uint8Array([0x41]), undefined], expected: 'A' },
        { args: [new Uint8Array([0x41]), null], expected: 'A' },
        { prefix: new Uint8Array([0xE2, 0x82]), args: [], expected: '\uFFFD' },
        { prefix: new Uint8Array([0xE2]),
          args: [undefined, {stream: true}], expected: '' },
        { args: [null], exception: 'TypeError' },
    ],
};

let ignoreBOM_tsuite = {
    name: "TextDecoder() ignoreBOM tests",
    T: async (params) => {
        let td = new TextDecoder('utf8', params.opts);
        let te = new TextEncoder();

        let res = te.encode(td.decode(params.value));

        if (!compareArray(res, params.expected)) {
            throw Error(`unexpected output "${res}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    tests: [
        { value: new Uint8Array([239, 187, 191, 50]),
          opts: {ignoreBOM: true},
          expected: [239, 187, 191, 50] },
        { value: new Uint8Array([239, 187, 191, 50]),
          opts: {ignoreBOM: false},
          expected: [50] },
        { value: new Uint8Array([239, 187, 191, 50]),
          opts: {},
          expected: [50] },
    ],
};

let detached_tsuite = {
    name: "TextDecoder() detached buffer test",
    skip: () => !is_detach_available(),

    T: async (params) => {
        let td = new TextDecoder('utf-8');
        let uint8 = new Uint8Array([0]);

        detach(uint8.buffer);

        try {
            td.decode(uint8);

        } catch (e) {
            if (e.toString().startsWith('TypeError:')) {
                return 'SUCCESS';
            } else {
                throw e;
            }
        }

        throw Error('Expected TypeError not thrown');
    },

    tests: [
        { },
    ],
};

let detached_options_tsuite = {
    name: "TextDecoder() detach in options test",
    skip: () => !is_detach_available(),

    T: async (params) => {
        let td = new TextDecoder('utf-8');
        let uint8 = new Uint8Array(4096);
        let input = params.buffer ? uint8.buffer : uint8;
        let options = {
            get stream() {
                detach(uint8.buffer);
                return false;
            }
        };

        try {
            td.decode(input, options);

        } catch (e) {
            if (e.toString().startsWith('TypeError:')) {
                return 'SUCCESS';
            }

            throw e;
        }

        throw Error('Expected TypeError not thrown');
    },

    tests: [
        { buffer: false },
        { buffer: true },
    ],
};


run([
    stream_tsuite,
    fatal_tsuite,
    reuse_tsuite,
    args_tsuite,
    ignoreBOM_tsuite,
    detached_tsuite,
    detached_options_tsuite,
])
.then($DONE, $DONE);
