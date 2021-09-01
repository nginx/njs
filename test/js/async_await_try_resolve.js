async function af() {
    let key;

    try {
        key = await Promise.resolve("key");
        key += ": resolve";

    } catch (e) {
        key += ": exception";
    }

    return key;
};

af().then(v => console.log(v));
