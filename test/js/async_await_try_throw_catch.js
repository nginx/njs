async function af() {
    try {
        throw "try";

        console.log("shouldn't happen: try");
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
