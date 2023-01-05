function compareObjects(ref, obj) {
    if (!isObject(ref) || !isObject(obj)) {
        return ref === obj;
    }

    for (const key in ref) {
        if (!compareObjects(ref[key], obj[key])) {
            return false;
        }
    }

    return true;
}

function isObject(object) {
    return object != null && typeof object === 'object';
}
