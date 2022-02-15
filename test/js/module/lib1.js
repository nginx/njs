var state = {count:0}

function inc() {
    state.count++;
}

function get() {
    return state.count;
}

export default {inc, get}
