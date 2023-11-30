let xml = null;

if (typeof require == 'function'
    && typeof njs == 'object'
    && typeof njs.version == 'string')
{
    try {
        xml = require('xml');

    } catch (e) {
        // ignore
    }
}

function has_xml() {
    return xml;
}
