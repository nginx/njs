function resolve(value) {
    return new Promise(resolve => setTimeout(() => resolve(value), 0));
}

Promise.all([resolve(['one', 'two']), resolve(['three', 'four'])])
.then(
    (v) => {console.log(`resolved:${njs.dump(v)}`)},
    (v) => {console.log(`rejected:${njs.dump(v)}`)}
);
