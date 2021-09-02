async function test(name) {
    let k1, k2;

    switch (name) {
        case "First":
            k1 = await Promise.resolve("SUN");
            k2 = await Promise.resolve("MOON");
            break;

        case "Second":
            k1 = await Promise.resolve("CAT");
            k2 = await Promise.resolve("MOUSE");
            break;

        case "Third":
            k1 = await Promise.resolve("MAN");
            k2 = await Promise.resolve("WOMAN");
            break;

        default:
            break;
    }

    return `${name}: ${k1} ${k2}`;
};

Promise.all(['First', 'Second', 'Third'].map(v => test(v)))
.then(results => {
    console.log(results)
})
