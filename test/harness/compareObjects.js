function compareObjects(ref, obj) {
    if (!isObject(ref) || !isObject(obj)) {
        return ref === obj;
    }

    for (const key in ref) {
        if (!Object.prototype.hasOwnProperty.call(ref, key)) {
            continue;
        }

        if (!Object.prototype.hasOwnProperty.call(obj, key)) {
            return false;
        }

        if (!compareObjects(ref[key], obj[key])) {
            return false;
        }
    }

    return true;
}

function isObject(object) {
    return object !== null && typeof object === 'object';
}
