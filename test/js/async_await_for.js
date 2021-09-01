let stage = [];

async function f() {
    let sum = 0;

    stage.push(2);

    for (let x = 4; x < 14; x++) {
        sum += await new Promise((resolve, reject) => {resolve(x)});

        stage.push(x);
    }

    stage.push("end");

    return sum;
}

stage.push(1);

f().then(v => {console.log(v, stage.join(", "))})

stage.push(3);
