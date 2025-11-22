/*---
includes: [compatFs.js, compatNjs.js, runTsuite.js]
flags: [async]
---*/

import xml from 'xml';

let parse_tsuite = {
    name: "parse()",
    skip: () => (!has_njs()),
    T: async (params) => {
        let doc = xml.parse(params.doc);
        let r = params.get(doc);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {
        doc: `<note><to b=\"bar\" a= \"foo\" >Tove</to><from>Jani</from></note>`,
    },

    tests: [
        { get: (doc) => doc.nonexist, expected: undefined },
        { get: (doc) => doc.note.$name, expected: 'note' },
        { get: (doc) => doc.note.$text, expected: 'ToveJani' },
        { get: (doc) => doc.note.to.$text, expected: 'Tove' },
        { get: (doc) => doc.note.$tag$to.$text, expected: 'Tove' },
        { get: (doc) => doc.note.$attrs, expected: undefined },
        { get: (doc) => doc.note.to.$attrs.a, expected: 'foo' },
        { get: (doc) => doc.note.to.$attrs.b, expected: 'bar' },
        { get: (doc) => doc.note.to.$attr$b, expected: 'bar' },
        { get: (doc) => doc.note.$attr$a, expected: undefined },
        { get: (doc) => Array.isArray(doc.note.$tags), expected: true },
        { get: (doc) => doc.note.$tags[0].$text, expected: 'Tove' },
        { get: (doc) => doc.note.$tags[1].$text, expected: 'Jani' },
        { get: (doc) => doc.note.$tags$from[0].$text, expected: 'Jani' },
        { get: (doc) => doc.note.$parent, expected: undefined },
        { get: (doc) => doc.note.to.$parent.$name, expected: 'note' },
        { doc: `<n0:pdu xmlns:n0=\"http://a\"></n0:pdu>`,
          get: (doc) => doc.pdu.$ns,
          expected: 'http://a' },
        { doc: `<root><foo>FOO</foo><foo>BAR</foo></root>`,
          get: (doc) => doc.root.$tags$foo[0].$text,
          expected: 'FOO' },
        { doc: `<root><foo>FOO</foo><foo>BAR</foo></root>`,
          get: (doc) => doc.root.$tags$foo[1].$text,
          expected: 'BAR' },
        { doc: `<root><foo>FOO</foo><foo>BAR</foo></root>`,
          get: (doc) => doc.root.$tags$bar.length,
          expected: 0 },
        { doc: `<root><foo>FOO</foo><foo>BAR</foo></root>`,
          get: (doc) => doc.root.$tags.length,
          expected: 2 },
        { doc: `<r><a></a>TEXT</r>`,
          get: (doc) => doc.r.$text,
          expected: 'TEXT' },
        { doc: `<r><a></a>TEXT</r>`,
          get: (doc) => doc.$root.$text,
          expected: 'TEXT' },
        { doc: `<r>俄语<a></a>данные</r>`,
          get: (doc) => doc.r.$text[2],
          expected: 'д' },
        { doc: `<俄语 լեզու=\"ռուսերեն\">данные</俄语>`,
          get: (doc) => JSON.stringify([doc['俄语'].$name[1],doc['俄语']['$attr$լեզու'][7],doc['俄语'].$text[5]]),
          expected: '["语","ն","е"]' },

        { get: (doc) => JSON.stringify(Object.getOwnPropertyNames(doc)),
          expected: '["note"]' },
        { get: (doc) => JSON.stringify(Object.getOwnPropertyNames(doc.note)),
          expected: '["$name","$tags"]' },
        { get: (doc) => JSON.stringify(Object.getOwnPropertyNames(doc.note.to)),
          expected: '["$name","$attrs","$text"]' },

        { get: (doc) => JSON.stringify(doc.note.to.$attrs),
          expected: '{"b":"bar","a":"foo"}' },
        { get: (doc) => JSON.stringify(doc.note.$tags),
          expected: '[{"$name":"to","$attrs":{"b":"bar","a":"foo"},"$text":"Tove"},{"$name":"from","$text":"Jani"}]' },
        { get: (doc) => JSON.stringify(doc),
          expected: '{"note":{"$name":"note","$tags":[{"$name":"to","$attrs":{"b":"bar","a":"foo"},"$text":"Tove"},{"$name":"from","$text":"Jani"}]}}' },
        { get: (doc) => JSON.stringify(doc.note),
            expected: '{"$name":"note","$tags":[{"$name":"to","$attrs":{"b":"bar","a":"foo"},"$text":"Tove"},{"$name":"from","$text":"Jani"}]}' },

        { doc: `GARBAGE`,
          exception: 'Error: failed to parse XML (libxml2: "Start tag expected, \'<\' not found" at 1:1)' },
]};

let c14n_tsuite = {
    name: "c14n()",
    skip: () => (!has_njs()),
    T: async (params) => {
        let doc = xml.parse(params.doc);
        let r = params.call(doc);

        if (params.buffer) {
            r = new TextDecoder().decode(r);
        }

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {
        buffer: true,
        doc: `<n0:pdu xmlns:n0=\"http://a\"><n1:elem1 xmlns:n1=\"http://b\"><!-- comment -->foo</n1:elem1></n0:pdu>`,
    },

    tests: [
        { call: (doc) => xml.c14n(doc.pdu.elem1),
          expected: `<n1:elem1 xmlns:n0="http://a" xmlns:n1="http://b">foo</n1:elem1>` },
        { call: (doc) => xml.serialize(doc.pdu.elem1),
          expected: `<n1:elem1 xmlns:n0="http://a" xmlns:n1="http://b">foo</n1:elem1>` },
        { call: (doc) => xml.serializeToString(doc.pdu.elem1),
          buffer: false,
          expected: `<n1:elem1 xmlns:n0="http://a" xmlns:n1="http://b">foo</n1:elem1>` },
        { call: (doc) => xml.exclusiveC14n(doc.pdu.elem1),
          expected: `<n1:elem1 xmlns:n1="http://b">foo</n1:elem1>` },
        { call: (doc) => xml.exclusiveC14n(doc.pdu.elem1, null, true),
          expected: `<n1:elem1 xmlns:n1="http://b"><!-- comment -->foo</n1:elem1>` },
        { call: (doc) => xml.exclusiveC14n(doc.pdu.elem1, null, false, 'n1'),
          expected: `<n1:elem1 xmlns:n1="http://b">foo</n1:elem1>` },
        { call: (doc) => xml.exclusiveC14n(doc.pdu.elem1, null, false, 'a b c d e f g h i j'),
          expected: `<n1:elem1 xmlns:n1="http://b">foo</n1:elem1>` },
        { doc: `<note><to a="foo" b="bar">Tove</to><from>Jani</from></note>`,
          call: (doc) => xml.c14n(doc.note),
          expected: `<note><to a="foo" b="bar">Tove</to><from>Jani</from></note>` },
        { doc: `<note><to a="foo" b="bar">Tove</to><from>Jani</from></note>`,
          call: (doc) => xml.exclusiveC14n(doc.note),
          expected: `<note><to a="foo" b="bar">Tove</to><from>Jani</from></note>` },
        { doc: `<note><to a="foo" b="bar">Tove</to><from>Jani</from></note>`,
          call: (doc) => xml.exclusiveC14n(doc.note, doc.note.to),
          expected: `<note><from>Jani</from></note>` },
        { doc: `<r></r>`,
          call: (doc) => xml.exclusiveC14n(doc, 1),
          exception: 'TypeError: "excluding" argument is not a XMLNode object' },
]};

let modify_tsuite = {
    name: "modifying XML",
    skip: () => (!has_njs()),
    T: async (params) => {
        let doc = xml.parse(params.doc);
        let r = params.get(doc);

        if (r !== params.expected) {
            throw Error(`unexpected output "${r}" != "${params.expected}"`);
        }

        return 'SUCCESS';
    },

    opts: {
        doc: `<note><to b=\"bar\" a= \"foo\" >Tove</to><from>Jani</from></note>`,
    },

    tests: [
        { get: (doc) => {
            doc.note.setText('WAKA');
            return doc.note.$text;
          },
          expected: 'WAKA' },
        { get: (doc) => {
            doc.note.setText('WAKA');
            doc.note.setText('OVERWRITE');
            return doc.note.$text;
          },
          expected: 'OVERWRITE' },
        { get: (doc) => {
            let note = doc.note;
            note.setText('WAKA');
            note.setText('OVERWRITE');
            return note.$text;
          },
          expected: 'OVERWRITE' },
        { get: (doc) => {
            let note = doc.note;
            note.setText('WAKA');
            return doc.note.$text;
          },
          expected: 'WAKA' },
        { get: (doc) => {
            doc.note.setText('WAKA');
            return xml.serializeToString(doc);
          },
          expected: `<note>WAKA</note>` },
        { get: (doc) => {
            doc.note.$text = '<WA&KA>';
            return xml.serializeToString(doc);
          },
          expected: `<note>&lt;WA&amp;KA&gt;</note>` },
        { get: (doc) => {
            doc.note.$text = '';
            return xml.serializeToString(doc);
          },
          expected: `<note></note>` },
        { get: (doc) => {
            doc.note.setText('<WA&KA>');
            return doc.note.$text;
          },
          expected: '<WA&KA>' },
        { get: (doc) => {
            doc.note.setText('<WA&KA>');
            return xml.serializeToString(doc);
          },
          expected: `<note>&lt;WA&amp;KA&gt;</note>` },
        { get: (doc) => {
            doc.note.setText('"WAKA"');
            return xml.serializeToString(doc);
          },
          expected: `<note>"WAKA"</note>` },
        { get: (doc) => {
            doc.note.setText('');
            return doc.note.$text;
          },
          expected: '' },
        { get: (doc) => {
            doc.note.setText('');
            return xml.serializeToString(doc);
          },
          expected: `<note></note>` },
        { get: (doc) => {
            doc.note.setText(null);
            return doc.note.$text;
          },
          expected: '' },
        { get: (doc) => {
            doc.note.setText(undefined);
            return doc.note.$text;
          },
          expected: '' },
        { get: (doc) => {
            doc.note.removeText();
            return doc.note.$text;
          },
          expected: '' },
        { get: (doc) => {
            delete doc.note.$text;
            return xml.serializeToString(doc);
          },
          expected: `<note></note>` },
        { get: (doc) => {
            doc.note.removeText();
            return xml.serializeToString(doc);
          },
          expected: `<note></note>` },
        { get: (doc) => {
            let to = doc.note.to;
            doc.$root.$text = '';
            return to.$name;
          },
          expected: 'to' },
        { get: (doc) => {
            let to = doc.note.to;
            doc.$root.$text = '';
            return [to.$name, to.$text, to.$attr$b, to.$parent.$name].toString();
          },
          expected: 'to,Tove,bar,note' },
        { get: (doc) => {
            doc.note.to.setAttribute('aaa', 'foo');
            doc.note.to.setAttribute('bbb', '<bar\"');
            return xml.serializeToString(doc.note.to);
          },
          expected: `<to a="foo" aaa="foo" b="bar" bbb="&lt;bar&quot;">Tove</to>` },
        { get: (doc) => {
            doc.note.to.$attr$aaa = 'foo';
            doc.note.to.$attr$bbb = '<bar\"';
            return xml.serializeToString(doc.note.to);
          },
          expected: `<to a="foo" aaa="foo" b="bar" bbb="&lt;bar&quot;">Tove</to>` },
        { get: (doc) => {
            doc.note.to.$attr$aaa = 'foo';
            return doc.note.to.$attr$aaa;
          },
          expected: `foo` },
        { get: (doc) => {
            doc.note.to.setAttribute('aaa', 'foo');
            doc.note.to.setAttribute('aaa', 'foo2');
            return xml.serializeToString(doc.note.to);
          },
          expected: `<to a="foo" aaa="foo2" b="bar">Tove</to>` },
        { get: (doc) => {
            doc.note.to.removeAttribute('a');
            return xml.serializeToString(doc.note.to);
          },
          expected: `<to b="bar">Tove</to>` },
        { get: (doc) => {
            doc.note.to.removeAllAttributes();
            return xml.serializeToString(doc.note.to);
          },
          expected: `<to>Tove</to>` },
        { get: (doc) => {
            let attrs = doc.note.to.$attrs;
            doc.note.to.removeAllAttributes();
            return attrs.a;
          },
          expected: undefined },
        { get: (doc) => {
            let to = doc.note.to;
            let attrs = doc.note.to.$attrs;
            to.removeAllAttributes();
            return attrs.a;
          },
          expected: undefined },
        { get: (doc) => {
            delete doc.note.to.$attr$a;
            return xml.serializeToString(doc.note.to);
          },
          expected: `<to b="bar">Tove</to>` },
        { get: (doc) => {
            doc.note.to.setAttribute('a', null);
            return xml.serializeToString(doc.note.to);
          },
          expected: `<to b="bar">Tove</to>` },
        { get: (doc) => {
            doc.note.to.setAttribute('<', 'foo');
            return xml.serializeToString(doc.note.to);
          },
          exception: 'TypeError: attribute name "<" is not valid' },
        { get: (doc) => {
            let doc2 = xml.parse(`<n0:pdu xmlns:n0=\"http://a\"></n0:pdu>`);
            doc.note.addChild(doc2);
            return xml.serializeToString(doc);
            },
          expected: `<note xmlns:n0="http://a"><to a="foo" b="bar">Tove</to><from>Jani</from><n0:pdu></n0:pdu></note>` },
        { get: (doc) => {
            let doc2 = xml.parse(`<n0:pdu xmlns:n0=\"http://a\"></n0:pdu>`);
            doc.note.addChild(doc2);
            doc.note.addChild(doc2);
            return xml.serializeToString(doc);
            },
          expected: `<note xmlns:n0="http://a"><to a="foo" b="bar">Tove</to><from>Jani</from><n0:pdu></n0:pdu><n0:pdu></n0:pdu></note>` },
        { get: (doc) => {
            doc.note.removeChildren('to');
            return xml.serializeToString(doc);
          },
          expected: `<note><from>Jani</from></note>` },
        { get: (doc) => {
            let note = doc.note;
            note.removeChildren('to');
            return xml.serializeToString(doc);
          },
          expected: `<note><from>Jani</from></note>` },
        { get: (doc) => {
            delete doc.note.$tag$to;
            return xml.serializeToString(doc);
          },
          expected: `<note><from>Jani</from></note>` },
        { get: (doc) => {
            delete doc.note.to;
            return xml.serializeToString(doc);
          },
          expected: `<note><from>Jani</from></note>` },
        { get: (doc) => {
            doc.note.removeChildren('xxx');
            return xml.serializeToString(doc);
          },
          expected: `<note><to a="foo" b="bar">Tove</to><from>Jani</from></note>` },
        { get: (doc) => {
            delete doc.note.$tag$xxx;
            return xml.serializeToString(doc);
          },
          expected: `<note><to a="foo" b="bar">Tove</to><from>Jani</from></note>` },
        { doc: `<root><a>A</a><b>B</b><a>C</a></root>`,
          get: (doc) => {
            doc.$root.removeChildren('a');
            return xml.serializeToString(doc);
          },
          expected: `<root><b>B</b></root>` },
        { doc: `<root><a>A</a><b>B</b><a>C</a></root>`,
          get: (doc) => {
            doc.$root.removeChildren();
            return xml.serializeToString(doc);
          },
          expected: `<root></root>` },
        { doc: `<root><a>A</a><b>B</b><a>C</a></root>`,
          get: (doc) => {
            doc.$root.$tags = [];
            return xml.serializeToString(doc);
          },
          expected: `<root></root>` },
        { doc: `<root><a>A</a><b>B</b><a>C</a></root>`,
          get: (doc) => {
            doc.$root.$tags$ = [];
            return xml.serializeToString(doc);
          },
          expected: `<root></root>` },
        { doc: `<root><a>A</a><b>B</b><a>C</a></root>`,
          get: (doc) => {
            delete doc.$root.$tag$a;
            return xml.serializeToString(doc);
          },
          expected: `<root><b>B</b></root>` },
        { get: (doc) => {
            doc.note.$tags = [doc.note.to];
            return xml.serializeToString(doc);
            },
          expected: `<note><to a="foo" b="bar">Tove</to></note>` },
        { get: (doc) => {
            let doc2 = xml.parse(`<n0:pdu xmlns:n0=\"http://a\"></n0:pdu>`);
            doc.note.$tags = [doc.note.to, doc2];
            return xml.serializeToString(doc);
            },
          expected: `<note xmlns:n0="http://a"><to a="foo" b="bar">Tove</to><n0:pdu></n0:pdu></note>` },
        { get: (doc) => {
            let doc2 = xml.parse(`<n0:pdu xmlns:n0=\"http://a\"></n0:pdu>`);
            doc.note.$tags = [doc2, doc.note.to];
            return xml.serializeToString(doc);
            },
          expected: `<note xmlns:n0="http://a"><n0:pdu></n0:pdu><to a="foo" b="bar">Tove</to></note>` },
]};

run([
    parse_tsuite,
    c14n_tsuite,
    modify_tsuite,
])
.then($DONE, $DONE);
