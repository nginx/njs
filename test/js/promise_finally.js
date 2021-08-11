Promise.resolve('here')
.finally(() => {'nope'})
.then(v => {console.log(v)});

Promise.resolve('here')
.finally(() => {throw 'nope'})
.then(v => {console.log(v)});
