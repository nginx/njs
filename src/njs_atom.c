
/*
 * Copyright (C) Vadim Zhestkov
 * Copyright (C) F5, Inc.
 */


#include <njs_main.h>


typedef struct {
    uint32_t          cells[NJS_ATOM_HASH_MASK + 1];
    struct {
        uint32_t     hash_mask;
        uint32_t     elts_size;
        uint32_t     elts_count;
        uint32_t     elts_deleted_count;
    } descr;
    njs_flathsh_elt_t elts[NJS_ATOM_SIZE+NJS_ATOM_SYMBOL_KNOWN_MAX];
} njs_atom_hash_chunk_t;


#ifdef NJS_DEF_VW
    #undef NJS_DEF_VW
    #undef NJS_DEF_VS
#endif

#define NJS_DEF_VW(name) \
    .vw_ ## name = njs_symbol(&njs_atom.vs_Symbol_ ## name),

#define NJS_DEF_VS(name, str, flags, token) \
    .vs_ ## name = njs_string(str, flags, token),

njs_atom_values_t njs_atom = {
    #include <njs_atom_defs.h>
};


static njs_atom_hash_chunk_t njs_atom_hash_chunk = {
    .descr = {
        .hash_mask = NJS_ATOM_HASH_MASK,
        .elts_size = NJS_ATOM_SIZE+NJS_ATOM_SYMBOL_KNOWN_MAX,
        .elts_count = 0,
        .elts_deleted_count = 0,
    }
};


njs_flathsh_t njs_atom_hash = {
    .slot = &njs_atom_hash_chunk.descr,
};


uint32_t  njs_atom_hash_atom_id = 0;


static njs_int_t
njs_atom_hash_test(njs_flathsh_query_t *lhq, void *data)
{
    size_t       size;
    u_char       *start;
    njs_value_t  *name;

    name = data;

    if (name->type == NJS_STRING && ((njs_value_t *)lhq->value)->type ==
        NJS_STRING) {

        size = name->string.data->length;

        if (lhq->key.length != size) {
            return NJS_DECLINED;
        }

        start = (u_char *) name->string.data->start;

        if (memcmp(start, lhq->key.start, lhq->key.length) == 0) {
           return NJS_OK;
        } 
    }

    if (name->type == NJS_SYMBOL && ((njs_value_t *)lhq->value)->type ==
        NJS_SYMBOL) {

        if (name->atom_id == lhq->key_hash) {
            return NJS_OK;
        }
    }

    return NJS_DECLINED;
}


/*
 *  Here is used statically allocated hash with correct size.
 *  This hash is only filled in, and later used as read only one.
 *  So, alloc/free are never used here.
 */

const njs_flathsh_proto_t  njs_atom_hash_proto
    njs_aligned(64) =
{
    0,
    njs_atom_hash_test,
    NULL,
    NULL,
};


void
njs_atom_hash_init()
{
    u_char               *start;
    size_t               len;
    njs_uint_t           n;
    njs_value_t          *value, *values;
    njs_flathsh_query_t  lhq;

    if (njs_atom_hash_chunk.descr.elts_count != 0) {
        return;
    }

    values = &njs_atom.vw_invalid;

    lhq.replace = 0;
    lhq.proto = &njs_atom_hash_proto;
    lhq.pool = NULL; /* Not used. */

    for (n = 0; n < NJS_ATOM_SYMBOL_KNOWN_MAX + NJS_ATOM_SIZE; n++) {
        value = &values[n];

        value->string.atom_id = njs_atom_hash_atom_id++;

        if (value->type == NJS_SYMBOL) {
            lhq.key_hash = value->string.atom_id;

            lhq.value = (void *) value;

            /* never failed. */
            njs_flathsh_insert(&njs_atom_hash, &lhq);
        }


        if (value->type == NJS_STRING) {
            start = value->string.data->start;
            len = value->string.data->length;

            lhq.key_hash = njs_djb_hash(start, len);
            lhq.key.length = len;
            lhq.key.start = start;

            lhq.value = (void *) value;

            /* never failed. */
            njs_flathsh_insert(&njs_atom_hash, &lhq);
        }
    }

    return;
};


/*
 * value is always key: string or number or symbol.
 *
 * symbol always contians atom_id by construction. do nothing;
 * number if short number it is atomized by "| 0x80000000";
 * string if represents short number it is atomized by "| 0x80000000";
 *
 * for string and symbol atom_ids common range is uint32_t < 0x80000000.
 */

njs_int_t
njs_atom_atomize_key(njs_vm_t *vm, njs_value_t *value)
{
    double             num;
    uint32_t           hash_id;
    njs_int_t          ret;
    njs_value_t        val_str;
    const njs_value_t  *entry;

    switch (value->type) {
    case NJS_STRING:
        num = njs_key_to_index(value);
        if (njs_fast_path(njs_key_is_integer_index(num, value)) &&
            ((uint32_t) num) < 0x80000000)
        {
            value->atom_id = ((uint32_t) num) | 0x80000000;

        } else {
            hash_id = njs_djb_hash(value->string.data->start,
                                   value->string.data->size);

            entry = njs_lexer_keyword_find(vm, value->string.data->start,
                                           value->string.data->size,
                                           value->string.data->length,
                                           hash_id);
            if (njs_slow_path(entry == NULL)) {
                return NJS_ERROR;
            }

            /* TODO: if (<<value is string>>) <<try release>>(string) */
            *value = *entry;
        }
        break;

    case NJS_NUMBER:
        num = value->data.u.number;
        if (njs_fast_path(njs_key_is_integer_index(num, value)) &&
            ((uint32_t) num) < 0x80000000)
        {
            value->atom_id = ((uint32_t) num) | 0x80000000;

        } else {
            /* convert num to string, and atomize it. */
            ret = njs_number_to_string(vm, &val_str, value);
            if (ret != NJS_OK) {
                return ret;
            }

            if (val_str.atom_id == 0) {
                hash_id = njs_djb_hash(val_str.string.data->start,
                                       val_str.string.data->size);

                entry = njs_lexer_keyword_find(vm, val_str.string.data->start,
                                               val_str.string.data->size,
                                               val_str.string.data->length,
                                               hash_id);
                if (njs_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                value->atom_id = entry->atom_id;

            } else {
                value->atom_id = val_str.atom_id;
            }
        }
        break;
    default:
        /* NJS_SYMBOL: do nothing. */
    }

    return NJS_OK;
}


njs_int_t
njs_atom_atomize_key_s(njs_vm_t *vm, njs_value_t *value)
{
    njs_int_t            ret;
    njs_flathsh_query_t  lhq;

    lhq.replace = 0;
    lhq.proto = &njs_lexer_hash_proto;
    lhq.pool = vm->atom_hash_mem_pool;


    value->string.atom_id = (*vm->atom_hash_atom_id)++;

    if (value->type == NJS_SYMBOL) {
        lhq.key_hash = value->string.atom_id;

        lhq.value = (void *) value;

        ret = njs_flathsh_insert(vm->atom_hash, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert/replace failed");
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}
