/*---
includes: [compatWebcrypto.js, runTsuite.js]
flags: [async]
---*/

async function test(params) {
    let uuid = crypto.randomUUID();

    if (typeof uuid !== 'string') {
        throw Error(`randomUUID returned ${typeof uuid}, expected string`);
    }

    if (uuid.length !== 36) {
        throw Error(`randomUUID length ${uuid.length}, expected 36`);
    }

    let re = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
    if (!re.test(uuid)) {
        throw Error(`randomUUID format invalid: "${uuid}"`);
    }

    let uuid2 = crypto.randomUUID();

    if (uuid === uuid2) {
        throw Error(`randomUUID not unique: "${uuid}" === "${uuid2}"`);
    }

    if (!re.test(uuid2)) {
        throw Error(`randomUUID second call format invalid: "${uuid2}"`);
    }

    return 'SUCCESS';
}

let randomUUID_tsuite = {
    name: "crypto.randomUUID()",
    skip: () => (!has_webcrypto()),
    T: test,
    prepare_args: (args) => args,

    tests: [
        { },
]};

run([randomUUID_tsuite])
.then($DONE, $DONE);
