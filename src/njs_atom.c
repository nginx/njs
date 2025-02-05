
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
