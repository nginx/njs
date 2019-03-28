function gen_export() {
    var _export = {};

    _export.sum = function(a, b) { return a + b; }
    _export.prod = function(a, b) { return a * b; }

    return _export;
}

export default gen_export();
