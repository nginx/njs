function pr(x) {
    return new Promise(resolve => {resolve(x)});
}

async function add(x) {
    const a = pr(20);
    const b = pr(50);
    return await a + await b;
}

add(50).then(v => {console.log(v)});
