async function af() {
    try {
        await new Promise(function(resolve, reject) {
            reject("reject");
        });

        console.log("shouldn't happen");
    }
    catch (v) {
        console.log(v);
    }
    finally {
        console.log("finally");
    }

    return "end";
};

af().then(v => console.log(v));
