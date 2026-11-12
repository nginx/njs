/*---
includes: []
flags: []
---*/

if (typeof njs == 'object' && typeof njs.on == 'function') {
    (function() {
        let o = {v: 'x'};

        njs.on('exit', function() {
            njs.on('exit', null);

            let a = [];

            for (let i = 0; i < 100; i++) {
                a.push({i});
            }

            if (o.v !== 'x') {
                throw Error('closure is lost');
            }
        });
    })();
}
