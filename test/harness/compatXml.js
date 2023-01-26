let xml = null;

if (typeof require == 'function'
    && typeof njs == 'object'
    && typeof njs.version == 'string')
{
    xml = require('xml');
}
