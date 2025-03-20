/*---
includes: [compatNjs.js]
flags: []
paths: []
---*/

import xml from 'xml';

let data = `<?xml version="1.0"?>
<!DOCTYPE foo [
<!ENTITY c PUBLIC "bar" "extern_entity.txt">
]>
<root>&c;</root>
`;

if (has_njs()) {
    let doc = xml.parse(data);
    assert.sameValue(doc.$root.$text, "");
}
