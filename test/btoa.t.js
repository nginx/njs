
/*---
includes: [runTsuite.js, compareArray.js]
flags: [async]
---*/

function codePoints(s) {
    let cp = [];
    for (var i = 0; i < s.length; i++) {
        cp.push(s.codePointAt(i));
    }

    return cp;
}

let btoa_tsuite = {
    name: "btoa() tests",
    T: async (params) => {
        let res = btoa(params.value);

        if (res !== params.expected) {
            throw Error(`unexpected output "${res}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    tests: [
        { value: undefined, expected: "dW5kZWZpbmVk" },
        { value: "", expected: "" },
        { value: "hello", expected: "aGVsbG8=" },
        { value: "\x00", expected: "AA==" },
        { value: "\x00\x01", expected: "AAE=" },
        { value: "\x00\x01\x02", expected: "AAEC" },
        { value: "\x00\xfe\xff", expected: "AP7/" },
        { value: String.fromCodePoint(0x100),
          exception: 'TypeError: invalid character (> U+00FF)' },
        { value: String.fromCodePoint(0x00, 0x100),
          exception: 'TypeError: invalid character (> U+00FF)' },
        { value: String.fromCodePoint(0x00, 0x01, 0x100),
          exception: 'TypeError: invalid character (> U+00FF)' },
    ],
};

let atob_tsuite = {
    name: "atob() tests",
    T: async (params) => {
        let res = codePoints(atob(params.value));

        if (!compareArray(res, params.expected)) {
            throw Error(`unexpected output "${res}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    tests: [
        { value: "", expected: [] },
        { value: "AAE=", expected: [0, 1] },
        { value: "AAEC", expected: [0, 1, 2] },
        { value: "AP7/", expected: [0, 254, 255] },
        { value: "dW5kZWZpbmVk", expected: codePoints("undefined") },

        /* Forgiving-base64 ignores missing padding. */

        { value: "aGVsbG8=", expected: codePoints("hello") },
        { value: "aGVsbG8", expected: codePoints("hello") },
        { value: "TQ==", expected: codePoints("M") },
        { value: "TQ", expected: codePoints("M") },

        /* Forgiving-base64 ignores ASCII whitespace. */

        { value: "CDRW", expected: [8, 52, 86] },
        { value: " CDRW", expected: [8, 52, 86] },
        { value: "C DRW", expected: [8, 52, 86] },
        { value: "CD RW", expected: [8, 52, 86] },
        { value: "CDR W", expected: [8, 52, 86] },
        { value: "CDRW    ", expected: [8, 52, 86] },
        { value: " C D R W ", expected: [8, 52, 86] },
        { value: "\tCDRW", expected: [8, 52, 86] },
        { value: "CD\nRW", expected: [8, 52, 86] },
        { value: "CDRW\r", expected: [8, 52, 86] },
        { value: "CD\fRW", expected: [8, 52, 86] },
        { value: "\t\n\f\r CDRW \r\f\n\t", expected: [8, 52, 86] },
        { value: "    ", expected: [] },
        { value: "\t\n\f\r ", expected: [] },

        /* Invalid input. */

        { value: undefined,
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
        { value: "=",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
        { value: "==",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
        { value: "===",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
        { value: "====",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
        { value: "AA@",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
        { value: "@",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
        { value: "A==A",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },

        /* Only ASCII whitespace is stripped: VT and NBSP are not. */

        { value: "\vCDRW",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
        { value: "CD\vRW",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
        { value: "\xa0CDRW",
          exception: 'TypeError: the string to be decoded is not correctly encoded' },
    ],
};

run([
    btoa_tsuite,
    atob_tsuite,
])
.then($DONE, $DONE);
