import re, os

global_keywords = {
    # Values.

    "null": 1,
    "false": 1,
    "true": 1,
    "undefined": 0,

    # Operators.

    "in": 1,
    "of": 0,
    "typeof": 1,
    "instanceof": 1,
    "void": 1,
    "new": 1,
    "delete": 1,
    "yield": 1,

    # Statements.

    "var": 1,
    "if": 1,
    "else": 1,
    "while": 1,
    "do": 1,
    "for": 1,
    "break": 1,
    "continue": 1,
    "switch": 1,
    "case": 1,
    "default": 1,
    "function": 1,
    "return": 1,
    "with": 1,
    "try": 1,
    "catch": 1,
    "finally": 1,
    "throw": 1,

    # Module.

    "import": 1,
    "export": 1,

    # Reserved words.

    "meta": 0,

    "from": 0,

    "this": 1,
    "arguments": 0,
    "eval": 0,

    "target": 0,

    "await": 1,
    "async": 0,
    "class": 1,
    "const": 1,
    "debugger": 1,
    "enum": 1,
    "extends": 1,
    "implements": 1,
    "interface": 1,
    "let": 1,
    "package": 1,
    "private": 1,
    "protected": 1,
    "public": 1,
    "static": 1,
    "super": 1
}


class Table:
    def __init__(self, header):
        self.buffer = []
        self.header = header

    def add(self, data):
        self.buffer.append(data)

    def create(self, newlines = False):
        result = []
        data = self.buffer

        result.append("static const {}[{}] =\n{{\n".format(self.header,
                                                           len(data)))

        last = len(data) - 1

        for idx in range(len(data)):
            result.append("    {},\n{}".format(data[idx],
                                    ("\n" if newlines and idx != last else "")))

        result.append("};")

        return result

class SHS:
    def __init__(self, data):
        self.data = data

    def test(self, idx_from, idx_to):
        stat = []

        for i in range(idx_from, idx_to):
            mx = 0
            used = 0
            result = {}

            for entry in self.data:
                idx = self.make_id(entry['key'], i)

                if idx not in result:
                    used += 1
                    result[idx] = 0

                result[idx] += 1

                if result[idx] > mx:
                    mx = result[idx]

            stat.append([mx, used, i])

        stat.sort(key = lambda entr: entr[0])
        best = stat[0]

        print("Max deep {}; Used {} of {}".format(*best))

        return best[2]

    def make_id(self, key, table_size):
        key = key.lower()
        return (((ord(key[0]) * ord(key[-1])) + len(key)) % table_size) + 1

    def make(self, best_size):
        self.table_size = best_size

        self.table = [[] for _ in range(self.table_size + 1)]

        for e in self.data:
            idx = self.make_id(e['key'], self.table_size)

            self.table[idx].append(e)
            self.table[idx].sort(key = lambda entr: len(entr['key']))

    def build(self):
        result = {}
        unused = []

        result[0] = [None, "NULL", self.table_size, 0, True]

        for key in range(1, self.table_size + 1):
            if not self.table[key]:
                unused.append(key)
                continue

            e = self.table[key].pop(0)
            result[key] = [e["key"], e["value"], len(e["key"]), 0, True]

        self.idx = self.table_size
        self.unused = unused
        self.unused_pos = 0

        for key in range(1, self.table_size + 1):
            if not self.table[key]:
                continue

            last_entry = result[key]

            for e in self.table[key]:
                last_entry[3] = self.next_free_pos()

                new_entry = [e["key"], e["value"], len(e["key"]), 0, False]

                result[last_entry[3]] = new_entry
                last_entry = new_entry

        return result

    def next_free_pos(self):
        if len(self.unused) > self.unused_pos:
            idx = self.unused[self.unused_pos]
            self.unused_pos += 1
            return idx

        self.idx += 1
        return self.idx


if __name__ == "__main__":
    data_name = "njs_lexer_kws"

    def enum(name):
        name = re.sub(r"[^a-zA-Z0-9_]", "_", name)
        return "NJS_TOKEN_" + name.upper()

    def kw_create():
        t = Table("njs_keyword_t " + data_name)

        for k, v in sorted(global_keywords.items()):
            t.add("{{\n"
                  "        .entry = {{ njs_str(\"{}\") }},\n"
                  "        .type = {},\n"
                  "        .reserved = {}\n"
                  "    }}"
                  .format(k, enum(k), v))

        return t.create(True)

    def entries_create():
        shs = SHS([{ "key": kw,
                     "value": "&{}[{}]".format(data_name, i) }
                   for i, kw in enumerate(sorted(global_keywords.keys()))])

        best_size = shs.test(5, 128)
        shs.make(best_size)
        lst = shs.build()

        t = Table("njs_lexer_keyword_entry_t njs_lexer_keyword_entries")

        for kw in range(shs.idx + 1):
            if kw not in lst:
                t.add("{ NULL, NULL, 0, 0 }")
                continue

            key_val = "\"{}\"".format(lst[kw][0]) if lst[kw][0] else "NULL"
            t.add("{{ {}, {}, {}, {} }}".format(key_val, *lst[kw][1:]))

        return t.create()

    content = ["""
/*
 * Copyright (C) Nginx, Inc.
 *
 * Do not edit, generated by: utils/lexer_keyword.py.
 */


#ifndef _NJS_LEXER_TABLES_H_INCLUDED_
#define _NJS_LEXER_TABLES_H_INCLUDED_


""",
    "".join(kw_create()),
    "\n\n\n",
    "".join(entries_create()),
    "\n\n\n#endif /* _NJS_LEXER_TABLES_H_INCLUDED_ */\n"]

    out = "".join(content)
    print(out)

    fn = os.path.join(os.path.dirname(__file__), "../src/njs_lexer_tables.h")

    with open(fn, 'w') as fh:
        fh.write(out)
