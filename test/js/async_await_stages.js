function pr(x) {
    return new Promise(resolve => {resolve(x)})
    .then(v => v).then(v => v);
}

let stage = [];

async function f() {
    let sum = 0;

    stage.push(2);

    const a1 = await pr(10);

    stage.push(4);

    const a2 = await pr(20);

    stage.push(5);

    return a1 + a2;
}

stage.push(1);

f().then(v => {console.log(v, stage.join(", "))})

stage.push(3);
