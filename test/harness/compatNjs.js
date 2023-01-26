function has_njs(required_version) {
    if (typeof njs != 'object'
        || typeof njs.version != 'string'
        || typeof njs.version_number != 'number')
    {
        return false;
    }

    if (required_version) {
        let req_number = required_version.split('.')
                         .map(v => Number(v)).reduce((acc, v) => acc * 256 + v, 0);

        return njs.version_number <= req_number;
    }


    return true;
}
