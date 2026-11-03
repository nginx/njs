/*---
includes: [compatFs.js]
flags: [async]
---*/

const fname = `${test_dir}/read_file_error`;
const size = 1024 * 1024;

function memorySize() {
    return typeof njs != 'undefined' && njs.engine == 'njs'
           ? njs.memoryStats.size : 0;
}

async function test() {
    fs.writeFileSync(fname, 'x'.repeat(size));

    try {
        const encodings = [undefined, 'utf8'];
        const modes = ['sync', 'callback', 'promise'];

        for (let i = 0; i < encodings.length; i++) {
            for (let j = 0; j < modes.length; j++) {
                const encoding = encodings[i];
                const mode = modes[j];
                const before = memorySize();
                let error;

                try {
                    const options = {flag: 'a', encoding};

                    if (mode == 'sync') {
                        fs.readFileSync(fname, options);

                    } else if (mode == 'callback') {
                        await new Promise((resolve, reject) => {
                            fs.readFile(fname, options, (err, data) => {
                                if (err) {
                                    reject(err);
                                } else {
                                    resolve(data);
                                }
                            });
                        });

                    } else {
                        await fsp.readFile(fname, options);
                    }

                } catch (e) {
                    error = e;
                }

                assert.sameValue(error.code, 'EBADF');
                assert.sameValue(error.syscall, 'read');
                assert.sameValue(error.path, fname);
                assert(memorySize() - before < size / 2,
                       'failed read retained its file buffer');
            }
        }

        assert.sameValue(fs.readFileSync(fname, 'utf8').length, size);

    } finally {
        fs.unlinkSync(fname);
    }
}

test().then($DONE, $DONE);
