/*---
includes: [runTsuite.js]
flags: [async]
---*/

import zlib from 'zlib';

let deflateSync_tsuite = {
    name: "deflateSync()/deflateRawSync() tests",
    skip: () => !zlib.deflateRawSync,
    T: async (params) => {
        const method = params.raw ? zlib.deflateRawSync : zlib.deflateSync;
        const r = method(params.value, params.options).toString('base64');

        if (r.length !== params.expected.length) {
            throw Error(`unexpected "${r}" length ${r.length} != ${params.expected.length}`);
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: { raw: true },

    tests: [
        { value: 'WAKA', expected: 'C3f0dgQA' },
        { value: 'αβγ', expected: 'O7fx3KZzmwE=' },
        { value: new Uint8Array([0x57, 0x41, 0x4b, 0x41]), expected: 'C3f0dgQA' },
        { value: Buffer.from([0x57, 0x41, 0x4b, 0x41]), expected: 'C3f0dgQA' },
        { value: 'WAKA', options: {level: zlib.constants.Z_NO_COMPRESSION}, expected: 'AQQA+/9XQUtB' },
        { value: 'αβγ', options: {level: zlib.constants.Z_NO_COMPRESSION}, expected: 'AQYA+f/Osc6yzrM=' },
        { value: 'WAKA'.repeat(10), options: {strategy: zlib.constants.Z_FIXED}, expected: 'C3f0dgwnAgMA' },
        { value: 'WAKA'.repeat(10), options: {strategy: zlib.constants.Z_RLE},
          expected: 'BcExAQAAAMKgbNwLYP8mwmQymUwmk8lkcg==' },
        { value: 'WAKA'.repeat(35), options: {strategy: zlib.constants.Z_RLE, memLevel: 1},
          expected: 'BMExAQAAAMKgbNwLYP8mwmQymUwmk8lkMplMJpPJZDKZTCaTyWQymUwmk+lzDHf0dgx39HYMd/R2BAA=' },
        { value: 'WAKA'.repeat(35), options: {strategy: zlib.constants.Z_RLE, memLevel: 8},
          expected: 'BcExAQAAAMKgbNwLYP8mwmQymUwmk8lkMplMJpPJZDKZTCaTyWQymUwmk8lkMjk=' },
        { value: 'WAKA', raw: false, expected: 'eJwLd/R2BAAC+gEl' },
        { value: 'αβγ', raw: false, expected: 'eJw7t/HcpnObAQ/sBIE=' },

        { value: 'WAKA', options: {level: 10}, exception: 'RangeError: level must be in the range -1..9' },
        { value: 'WAKA', options: {strategy: 10}, exception: 'RangeError: unknown strategy: 10' },
        { value: 'WAKA', options: {memLevel: 10}, exception: 'RangeError: memLevel must be in the range 1..9' },
        { value: 'WAKA', options: {windowBits: 99}, exception: 'RangeError: windowBits must be in the range -15..-9' },
]};

let inflateSync_tsuite = {
    name: "inflateSync()/inflateRawSync() tests",
    skip: () => !zlib.inflateRawSync || !zlib.deflateRawSync,
    T: async (params) => {
        const method = params.raw ? zlib.inflateRawSync : zlib.inflateSync;
        const r = method(params.value, params.options).toString();

        if (r.length !== params.expected.length) {
            throw Error(`unexpected "${r}" length ${r.length} != ${params.expected.length}`);
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: { raw: true },

    tests: [
        { value: Buffer.from('C3f0dgQA', 'base64'), expected: 'WAKA' },
        { value: Buffer.from('C3f0dgQA', 'base64'), expected: 'WAKA' },
        { value: new Uint8Array([0x0b, 0x77, 0xf4, 0x76, 0x04, 0x00]), expected: 'WAKA' },
        { value: Buffer.from('eJwLd/R2BAAC+gEl', 'base64'), raw: false, expected: 'WAKA' },
        { value: Buffer.from('eJw7t/HcpnObAQ/sBIE=', 'base64'), raw: false, expected: 'αβγ' },
        { value: zlib.deflateRawSync('WAKA'), expected: 'WAKA' },
        { value: zlib.deflateRawSync('αβγ'), expected: 'αβγ' },
        { value: zlib.deflateRawSync('WAKA', {dictionary: Buffer.from('WAKA')}), options: {dictionary: Buffer.from('WAKA')},
          expected: 'WAKA' },
        { value: zlib.deflateRawSync('αβγ', {dictionary: Buffer.from('αβγ')}), options: {dictionary: Buffer.from('αβγ')},
          expected: 'αβγ' },
        { value: zlib.deflateRawSync('αβγ'.repeat(56), {chunkSize: 64}), expected: 'αβγ'.repeat(56) },
        { value: zlib.deflateRawSync('WAKA'.repeat(1024)), expected: 'WAKA'.repeat(1024) },
        { value: zlib.deflateRawSync('αβγ'.repeat(1024)), expected: 'αβγ'.repeat(1024) },
        { value: zlib.deflateRawSync('WAKA'.repeat(1024)), options: {chunkSize: 64}, expected: 'WAKA'.repeat(1024) },
        { value: zlib.deflateRawSync('αβγ'.repeat(1024)), options: {chunkSize: 64}, expected: 'αβγ'.repeat(1024) },

        { value: Buffer.from('C3f0dgQA', 'base64'), options: {chunkSize: 0},
          exception: 'RangeError: chunkSize must be >= 64' },
        { value: Buffer.from('C3f0dgQA', 'base64'), options: {windowBits: 0},
          exception: 'RangeError: windowBits must be in the range -15..-8' },

        { value: zlib.deflateRawSync('WAKA', {dictionary: Buffer.from('WAKA')}),
          exception: 'InternalError: failed to inflate the compressed data: invalid distance too far back' },
]};

run([
    deflateSync_tsuite,
    inflateSync_tsuite,
])
.then($DONE, $DONE);
