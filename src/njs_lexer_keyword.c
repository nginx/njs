
/*
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>
#include <njs_lexer_tables.h>


njs_inline int
njs_lexer_keyword_hash(const u_char *key, size_t size, size_t table_size)
{
    return ((((key[0] * key[size - 1]) + size) % table_size) + 0x01);
}


njs_inline const njs_lexer_keyword_entry_t *
njs_lexer_keyword_entry(const njs_lexer_keyword_entry_t *root,
    const u_char *key, size_t length)
{
    const njs_lexer_keyword_entry_t  *entry;

    entry = root + njs_lexer_keyword_hash(key, length, root->length);

    while (entry->key != NULL) {
        if (entry->length == length) {
            if (strncmp(entry->key, (char *) key, length) == 0) {
                return entry;
            }

            entry = &root[entry->next];

        } else if (entry->length > length) {
            return NULL;

        } else {
            entry = &root[entry->next];
        }
    }

    return NULL;
}


const njs_lexer_keyword_entry_t *
njs_lexer_keyword(const u_char *key, size_t length)
{
    const njs_lexer_keyword_entry_t  *entry;

    entry = njs_lexer_keyword_entry(njs_lexer_keyword_entries, key, length);
    if (njs_slow_path(entry == NULL)) {
        return NULL;
    }

    return entry;
}


njs_int_t
njs_lexer_keywords(njs_arr_t *list)
{
    njs_str_t   *kw;
    njs_uint_t  i;

    for (i = 0; i < sizeof(njs_lexer_kws) / sizeof(njs_keyword_t); i++) {
        kw = njs_arr_add(list);
        if (njs_slow_path(kw == NULL)) {
            return NJS_ERROR;
        }

        *kw = njs_lexer_kws[i].entry.name;
    }

    return NJS_OK;
}
