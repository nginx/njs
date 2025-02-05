
/*
 * Copyright (C) Vadim Zhestkov
 * Copyright (C) F5, Inc.
 */


#include <njs_main.h>


#ifdef NJS_DEF_VW
    #undef NJS_DEF_VW
    #undef NJS_DEF_VS
#endif

#define NJS_DEF_VW(name) \
    .vw_ ## name = njs_symval(name),

#define NJS_DEF_VS(name) \
    .vs_ ## name = njs_strval(name),

const njs_atom_values_t njs_atom = {
    #include <njs_atom_defs.h>
};


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
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


njs_int_t
njs_atom_hash_init(njs_vm_t *vm)
{
    u_char               *start;
    size_t               len;
    njs_int_t            ret;
    njs_uint_t           n;
    const njs_value_t    *value, *values;
    njs_flathsh_query_t  lhq;

    values = &njs_atom.vw_invalid;

    njs_lvlhsh_init(vm->atom_hash);

    lhq.replace = 0;
    lhq.proto = &njs_atom_hash_proto;
    lhq.pool = vm->mem_pool;

    for (n = 0; n < NJS_ATOM_SIZE; n++) {
        value = &values[n];

        if (value->type == NJS_SYMBOL) {
            lhq.key_hash = value->string.atom_id;

            lhq.value = (void *) value;

            ret = njs_flathsh_insert(vm->atom_hash, &lhq);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_internal_error(vm, "flathsh insert/replace failed");
                return NJS_ERROR;
            }
        }


        if (value->type == NJS_STRING) {
            start = value->string.data->start;
            len = value->string.data->length;

            lhq.key_hash = njs_djb_hash(start, len);
            lhq.key.length = len;
            lhq.key.start = start;

            lhq.value = (void *) value;

            ret = njs_flathsh_insert(vm->atom_hash, &lhq);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_internal_error(vm, "flathsh insert/replace failed");
                return NJS_ERROR;
            }
        }
    }

    return NJS_OK;
};
