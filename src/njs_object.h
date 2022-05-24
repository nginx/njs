
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_OBJECT_H_INCLUDED_
#define _NJS_OBJECT_H_INCLUDED_


typedef enum {
    NJS_OBJECT_PROP_DESCRIPTOR,
    NJS_OBJECT_PROP_GETTER,
    NJS_OBJECT_PROP_SETTER,
} njs_object_prop_define_t;


struct njs_object_init_s {
    const njs_object_prop_t     *properties;
    njs_uint_t                  items;
};


typedef struct njs_traverse_s  njs_traverse_t;

struct njs_traverse_s {
    struct njs_traverse_s      *parent;
    njs_object_prop_t          *prop;

    njs_value_t                value;
    njs_array_t                *keys;
    int64_t                    index;

#define NJS_TRAVERSE_MAX_DEPTH 32
};


typedef njs_int_t (*njs_object_traverse_cb_t)(njs_vm_t *vm,
    njs_traverse_t *traverse, void *ctx);


njs_object_t *njs_object_alloc(njs_vm_t *vm);
njs_object_t *njs_object_value_copy(njs_vm_t *vm, njs_value_t *value);
njs_object_value_t *njs_object_value_alloc(njs_vm_t *vm, njs_uint_t index,
    size_t extra,const njs_value_t *value);
njs_array_t *njs_object_enumerate(njs_vm_t *vm, const njs_object_t *object,
    njs_object_enum_t kind, njs_object_enum_type_t type, njs_bool_t all);
njs_array_t *njs_object_own_enumerate(njs_vm_t *vm, const njs_object_t *object,
    njs_object_enum_t kind, njs_object_enum_type_t type, njs_bool_t all);
njs_int_t njs_object_traverse(njs_vm_t *vm, njs_object_t *object, void *ctx,
    njs_object_traverse_cb_t cb);
njs_int_t njs_object_hash_create(njs_vm_t *vm, njs_lvlhsh_t *hash,
    const njs_object_prop_t *prop, njs_uint_t n);
njs_int_t njs_primitive_prototype_get_proto(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
njs_int_t njs_object_prototype_create(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_value_t *njs_property_prototype_create(njs_vm_t *vm, njs_lvlhsh_t *hash,
    njs_object_t *prototype);
njs_int_t njs_object_prototype_proto(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval);
njs_int_t njs_object_prototype_create_constructor(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
njs_value_t *njs_property_constructor_set(njs_vm_t *vm, njs_lvlhsh_t *hash,
    njs_value_t *constructor);
njs_int_t njs_object_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_int_t njs_object_length(njs_vm_t *vm, njs_value_t *value, int64_t *dst);

njs_int_t njs_prop_private_copy(njs_vm_t *vm, njs_property_query_t *pq);
njs_object_prop_t *njs_object_prop_alloc(njs_vm_t *vm, const njs_value_t *name,
    const njs_value_t *value, uint8_t attributes);
njs_int_t njs_object_property(njs_vm_t *vm, const njs_value_t *value,
    njs_lvlhsh_query_t *lhq, njs_value_t *retval);
njs_object_prop_t *njs_object_property_add(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *key, njs_bool_t replace);
njs_int_t njs_object_prop_define(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *name, njs_value_t *value, njs_object_prop_define_t type);
njs_int_t njs_object_prop_descriptor(njs_vm_t *vm, njs_value_t *dest,
    njs_value_t *value, njs_value_t *setval);
const char *njs_prop_type_string(njs_object_prop_type_t type);
njs_int_t njs_object_prop_init(njs_vm_t *vm, const njs_object_init_t* init,
    const njs_object_prop_t *base, njs_value_t *value, njs_value_t *retval);


njs_inline njs_bool_t
njs_is_data_descriptor(njs_object_prop_t *prop)
{
    return prop->writable != NJS_ATTRIBUTE_UNSET
           || njs_is_valid(&prop->value)
           || prop->type == NJS_PROPERTY_HANDLER;

}


njs_inline njs_bool_t
njs_is_accessor_descriptor(njs_object_prop_t *prop)
{
    return njs_is_function_or_undefined(&prop->getter)
           || njs_is_function_or_undefined(&prop->setter);
}


njs_inline njs_bool_t
njs_is_generic_descriptor(njs_object_prop_t *prop)
{
    return !njs_is_data_descriptor(prop) && !njs_is_accessor_descriptor(prop);
}


njs_inline void
njs_object_property_key_set(njs_lvlhsh_query_t *lhq, const njs_value_t *key,
    uint32_t hash)
{
    if (njs_is_symbol(key)) {

        lhq->key.length = 0;
        lhq->key.start = NULL;
        lhq->key_hash = njs_symbol_key(key);

    } else {

        /* string. */

        njs_string_get(key, &lhq->key);

        if (hash == 0) {
            lhq->key_hash = njs_djb_hash(lhq->key.start, lhq->key.length);

        } else {
            lhq->key_hash = hash;
        }
    }
}


njs_inline void
njs_object_property_init(njs_lvlhsh_query_t *lhq, const njs_value_t *key,
    uint32_t hash)
{
    lhq->proto = &njs_object_hash_proto;

    njs_object_property_key_set(lhq, key, hash);
}


njs_inline njs_int_t
njs_primitive_value_to_key(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *src)
{
    const njs_value_t  *value;

    switch (src->type) {

    case NJS_NULL:
        value = &njs_string_null;
        break;

    case NJS_UNDEFINED:
        value = &njs_string_undefined;
        break;

    case NJS_BOOLEAN:
        value = njs_is_true(src) ? &njs_string_true : &njs_string_false;
        break;

    case NJS_NUMBER:
        return njs_number_to_string(vm, dst, src);

    case NJS_SYMBOL:
    case NJS_STRING:
        /* GC: njs_retain(src); */
        value = src;
        break;

    default:
        return NJS_ERROR;
    }

    *dst = *value;

    return NJS_OK;
}


njs_inline njs_int_t
njs_value_to_key(njs_vm_t *vm, njs_value_t *dst, njs_value_t *value)
{
    njs_int_t    ret;
    njs_value_t  primitive;

    if (njs_slow_path(!njs_is_primitive(value))) {
        if (njs_slow_path(njs_is_object_symbol(value))) {
            /* should fail */
            value = njs_object_value(value);

        } else {
            ret = njs_value_to_primitive(vm, &primitive, value, 1);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            value = &primitive;
        }
    }

    return njs_primitive_value_to_key(vm, dst, value);
}


njs_inline njs_int_t
njs_key_string_get(njs_vm_t *vm, njs_value_t *key, njs_str_t *str)
{
    njs_int_t  ret;

    if (njs_slow_path(njs_is_symbol(key))) {
        ret = njs_symbol_descriptive_string(vm, key, key);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_string_get(key, str);

    return NJS_OK;
}


njs_inline njs_int_t
njs_object_length_set(njs_vm_t *vm, njs_value_t *value, int64_t length)
{
    njs_value_t  index;

    static const njs_value_t  string_length = njs_string("length");

    njs_value_number_set(&index, length);

    return njs_value_property_set(vm, value, njs_value_arg(&string_length),
                                  &index);
}


njs_inline njs_int_t
njs_object_string_tag(njs_vm_t *vm, njs_value_t *value, njs_value_t *tag)
{
    njs_int_t  ret;

    static const njs_value_t  to_string_tag =
                                njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG);

    ret = njs_value_property(vm, value, njs_value_arg(&to_string_tag), tag);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!njs_is_string(tag)) {
        return NJS_DECLINED;
    }

    return NJS_OK;
}


njs_inline njs_object_t *
_njs_object_proto_lookup(njs_object_t *proto, njs_value_type_t type)
{
    do {
        if (njs_fast_path(proto->type == type)) {
            break;
        }

        proto = proto->__proto__;
    } while (proto != NULL);

    return proto;
}


#define njs_object_proto_lookup(proto, vtype, ctype)                         \
    (ctype *) _njs_object_proto_lookup(proto, vtype)


extern const njs_object_type_init_t  njs_obj_type_init;


#endif /* _NJS_OBJECT_H_INCLUDED_ */
