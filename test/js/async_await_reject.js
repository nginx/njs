async function add(x) {
    return await new Promise((resolve, reject) => {reject(x)});
}

add(50).then(v => {console.log(v)});
