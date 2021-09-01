async function af() {
    try {
        await new Promise(function(resolve, reject) {
            reject("reject");
        });

        console.log("shouldn't happen: try");
    }
    finally {
        await new Promise(function(resolve, reject) {
            reject("finally reject");
        });

        console.log("shouldn't happen: finally");
    }

    return "shouldn't happen: end";
};

af().then(v => console.log(v));
