var state = {count:0}

function inc() {
    state.count++;
}

function get() {
    return state.count;
}

if (globalThis.lib1_is_loaded) {
    throw Error("lib1 already loaded");
    globalThis.lib1_is_loaded = true;
}

export default {inc, get}
