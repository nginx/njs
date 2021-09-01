async function add(x) {
    return await new Promise((resolve, reject) => {reject(x)})
                                                 .finally(() => console.log(x + 1));
}

add(50).then(v => {console.log(v)});
