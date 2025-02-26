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


run([
    stream_tsuite,
    fatal_tsuite,
    ignoreBOM_tsuite,
])
.then($DONE, $DONE);
