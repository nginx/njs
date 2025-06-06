
/*
 * Copyright (C) Vadim Zhestkov
 * Copyright (C) F5, Inc.
 */


#include <njs_main.h>


static njs_int_t njs_lexer_hash_test(njs_lvlhsh_query_t *lhq, void *data);
static njs_int_t njs_atom_hash_test(njs_flathsh_query_t *lhq, void *data);


const njs_value_t njs_atom[] = {
#define NJS_DEF_SYMBOL(_id, _s) njs_symval(_id, _s),
#define NJS_DEF_STRING(_id, _s, _typ, _tok) (njs_value_t) {                   \
    .string = {                                                               \
        .type = NJS_STRING,                                                   \
        .truth = njs_length(_s) ? 1 : 0,                                      \
        .atom_id = NJS_ATOM_STRING_ ## _id,                                   \
        .token_type = _typ,                                                   \
        .token_id = _tok,                                                     \
        .data = & (njs_string_t) {                                            \
            .start = (u_char *) _s,                                           \
            .length = njs_length(_s),                                         \
            .size = njs_length(_s),                                           \
        },                                                                    \
    }                                                                         \
},

    #include <njs_atom_defs.h>
};


const njs_lvlhsh_proto_t  njs_lexer_hash_proto
    njs_aligned(64) =
{
    NJS_LVLHSH_DEFAULT,
    njs_lexer_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


const njs_flathsh_proto_t  njs_atom_hash_proto
    njs_aligned(64) =
{
    0,
    njs_atom_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


static njs_int_t
njs_lexer_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    u_char       *start;
    njs_value_t  *name;

    name = data;

    njs_assert(name->type == NJS_STRING);

    if (lhq->key.length != name->string.data->size) {
        return NJS_DECLINED;
    }

    start = name->string.data->start;

    if (memcmp(start, lhq->key.start, lhq->key.length) == 0) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


njs_value_t *
njs_atom_find_or_add(njs_vm_t *vm, u_char *key, size_t size, size_t length,
    uint32_t hash)
{
    njs_int_t           ret;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    lhq.key.start = key;
    lhq.key.length = size;
    lhq.key_hash = hash;
    lhq.proto = &njs_lexer_hash_proto;

    ret = njs_lvlhsh_find(vm->atom_hash_current, &lhq);
    if (ret == NJS_OK) {
        return njs_prop_value(lhq.value);
    }

    ret = njs_lvlhsh_find(&vm->atom_hash_shared, &lhq);
    if (ret == NJS_OK) {
        return njs_prop_value(lhq.value);
    }

    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(vm->atom_hash_current, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    prop = lhq.value;

    ret = njs_string_create(vm, &prop->u.value, key, size);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    prop->u.value.string.atom_id = vm->atom_id_generator++;
    if (njs_atom_is_number(prop->u.value.string.atom_id)) {
        njs_internal_error(vm, "too many atoms");
        return NULL;
    }

    prop->u.value.string.token_type = NJS_KEYWORD_TYPE_UNDEF;

    return &prop->u.value;
}


static njs_value_t *
njs_atom_find_or_add_string(njs_vm_t *vm, njs_value_t *value,
    uint32_t hash)
{
    njs_int_t           ret;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    njs_assert(njs_is_string(value));

    lhq.key.start = value->string.data->start;
    lhq.key.length = value->string.data->size;
    lhq.key_hash = hash;
    lhq.proto = &njs_lexer_hash_proto;

    ret = njs_lvlhsh_find(vm->atom_hash_current, &lhq);
    if (ret == NJS_OK) {
        return njs_prop_value(lhq.value);
    }

    ret = njs_lvlhsh_find(&vm->atom_hash_shared, &lhq);
    if (ret == NJS_OK) {
        return njs_prop_value(lhq.value);
    }

    lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(vm->atom_hash_current, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    prop = lhq.value;

    prop->u.value = *value;

    prop->u.value.string.atom_id = vm->atom_id_generator++;
    if (njs_atom_is_number(prop->u.value.string.atom_id)) {
        njs_internal_error(vm, "too many atoms");
        return NULL;
    }

    prop->u.value.string.token_type = NJS_KEYWORD_TYPE_UNDEF;

    return &prop->u.value;
}


static njs_int_t
njs_atom_hash_test(njs_flathsh_query_t *lhq, void *data)
{
    size_t       size;
    u_char       *start;
    njs_value_t  *name;

    name = data;

    if (name->type == NJS_STRING
        && ((njs_value_t *) lhq->value)->type == NJS_STRING)
    {
        size = name->string.data->length;

        if (lhq->key.length != size) {
            return NJS_DECLINED;
        }

        start = (u_char *) name->string.data->start;

        if (memcmp(start, lhq->key.start, lhq->key.length) == 0) {
           return NJS_OK;
        }
    }

    if (name->type == NJS_SYMBOL
        && ((njs_value_t *) lhq->value)->type == NJS_SYMBOL)
    {
        if (lhq->key_hash == name->atom_id) {
            return NJS_OK;
        }
    }

    return NJS_DECLINED;
}


uint32_t
njs_atom_hash_init(njs_vm_t *vm)
{
    u_char               *start;
    size_t               len;
    njs_int_t            ret;
    njs_uint_t           n;
    const njs_value_t    *value, *values;
    njs_flathsh_query_t  lhq;

    values = &njs_atom[0];

    njs_lvlhsh_init(&vm->atom_hash_shared);

    lhq.replace = 0;
    lhq.proto = &njs_atom_hash_proto;
    lhq.pool = vm->mem_pool;

    for (n = 0; n < NJS_ATOM_SIZE; n++) {
        value = &values[n];

        if (value->type == NJS_SYMBOL) {
            lhq.key_hash = value->string.atom_id;

            ret = njs_flathsh_insert(&vm->atom_hash_shared, &lhq);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_internal_error(vm, "flathsh insert/replace failed");
                return 0xffffffff;
            }
        }

        if (value->type == NJS_STRING) {
            start = value->string.data->start;
            len = value->string.data->length;

            lhq.key_hash = njs_djb_hash(start, len);
            lhq.key.length = len;
            lhq.key.start = start;

            ret = njs_flathsh_insert(&vm->atom_hash_shared, &lhq);
            if (njs_slow_path(ret != NJS_OK)) {
                njs_internal_error(vm, "flathsh insert/replace failed");
                return 0xffffffff;
            }
        }

        *njs_prop_value(lhq.value) = *value;
    }

    vm->atom_hash_current = &vm->atom_hash_shared;

    return NJS_ATOM_SIZE;
}


njs_int_t
njs_atom_atomize_key(njs_vm_t *vm, njs_value_t *value)
{
    double             num;
    uint32_t           hash_id, u32;
    njs_int_t          ret;
    njs_value_t        val_str;
    const njs_value_t  *entry;

    njs_assert(value->atom_id == NJS_ATOM_STRING_unknown);

    switch (value->type) {
    case NJS_STRING:
        num = njs_key_to_index(value);
        u32 = (uint32_t) num;

        if (njs_fast_path(u32 == num && (u32 < 0x80000000)
                          && !(num == 0 && signbit(num))))
        {
            value->atom_id = njs_number_atom(u32);

        } else {
            hash_id = njs_djb_hash(value->string.data->start,
                                   value->string.data->size);

            entry = njs_atom_find_or_add_string(vm, value, hash_id);
            if (njs_slow_path(entry == NULL)) {
                return NJS_ERROR;
            }

            *value = *entry;
        }

        break;

    case NJS_NUMBER:
        num = njs_number(value);
        u32 = (uint32_t) num;

        if (njs_fast_path(u32 == num && (u32 < 0x80000000))) {
            value->atom_id = njs_number_atom(u32);

        } else {
            ret = njs_number_to_string(vm, &val_str, value);
            if (ret != NJS_OK) {
                return ret;
            }

            if (val_str.atom_id == NJS_ATOM_STRING_unknown) {
                hash_id = njs_djb_hash(val_str.string.data->start,
                                       val_str.string.data->size);

                entry = njs_atom_find_or_add(vm, val_str.string.data->start,
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

    case NJS_SYMBOL:
    default:
        /* do nothing. */
        break;
    }

    return NJS_OK;
}


njs_int_t
njs_atom_symbol_add(njs_vm_t *vm, njs_value_t *value)
{
    njs_int_t            ret;
    njs_flathsh_query_t  lhq;

    njs_assert(value->atom_id == NJS_ATOM_STRING_unknown);

    lhq.replace = 0;
    lhq.proto = &njs_lexer_hash_proto;
    lhq.pool = vm->mem_pool;

    value->atom_id = vm->atom_id_generator++;

    if (value->type == NJS_SYMBOL) {
        lhq.key_hash = value->atom_id;

        ret = njs_flathsh_insert(vm->atom_hash_current, &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "flathsh insert/replace failed");
            return NJS_ERROR;
        }

        *njs_prop_value(lhq.value) = *value;
    }

    return NJS_OK;
}
