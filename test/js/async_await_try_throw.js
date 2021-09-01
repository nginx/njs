async function af() {
    try {
        throw "try";

        console.log("shouldn't happen: try");
    }
    finally {
        console.log("finally");
    }

    return "shouldn't happen: end";
};

af().then(v => console.log(v));
