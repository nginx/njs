
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_OBJECT_H_INCLUDED_
#define _NJS_OBJECT_H_INCLUDED_


typedef enum {
    NJS_OBJECT_PROP_DESCRIPTOR = 0,
    NJS_OBJECT_PROP_VALUE = 1,
    NJS_OBJECT_PROP_GETTER = 2,
    NJS_OBJECT_PROP_SETTER = 3,
#define njs_prop_type(flags)  (flags & 3)
    NJS_OBJECT_PROP_CREATE = 4,
    NJS_OBJECT_PROP_ENUMERABLE = 8,
    NJS_OBJECT_PROP_CONFIGURABLE = 16,
    NJS_OBJECT_PROP_WRITABLE = 32,
    NJS_OBJECT_PROP_UNSET = 64,
    NJS_OBJECT_PROP_IS_STRING = 128,
#define NJS_OBJECT_PROP_VALUE_ECW (NJS_OBJECT_PROP_VALUE                     \
                                   | NJS_OBJECT_PROP_ENUMERABLE              \
                                   | NJS_OBJECT_PROP_CONFIGURABLE            \
                                   | NJS_OBJECT_PROP_WRITABLE)
#define NJS_OBJECT_PROP_VALUE_EC  (NJS_OBJECT_PROP_VALUE                     \
                                   | NJS_OBJECT_PROP_ENUMERABLE              \
                                   | NJS_OBJECT_PROP_CONFIGURABLE)
#define NJS_OBJECT_PROP_VALUE_CW  (NJS_OBJECT_PROP_VALUE                     \
                                   | NJS_OBJECT_PROP_CONFIGURABLE            \
                                   | NJS_OBJECT_PROP_WRITABLE)
#define NJS_OBJECT_PROP_VALUE_E   (NJS_OBJECT_PROP_VALUE                     \
                                   | NJS_OBJECT_PROP_ENUMERABLE)
#define NJS_OBJECT_PROP_VALUE_C   (NJS_OBJECT_PROP_VALUE                     \
                                   | NJS_OBJECT_PROP_CONFIGURABLE)
#define NJS_OBJECT_PROP_VALUE_W   (NJS_OBJECT_PROP_VALUE                     \
                                   | NJS_OBJECT_PROP_WRITABLE)
} njs_object_prop_flags_t;


struct njs_object_init_s {
    const njs_object_prop_init_t  *properties;
    njs_uint_t                    items;
};


typedef struct njs_traverse_s  njs_traverse_t;

struct njs_traverse_s {
    struct njs_traverse_s      *parent;
    njs_object_prop_t          *prop;
    uint32_t                   atom_id;

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
    uint32_t flags);
njs_array_t *njs_object_own_enumerate(njs_vm_t *vm, const njs_object_t *object,
    uint32_t flags);
njs_int_t njs_object_traverse(njs_vm_t *vm, njs_object_t *object, void *ctx,
    njs_object_traverse_cb_t cb);
njs_int_t njs_object_make_shared(njs_vm_t *vm, njs_object_t *object);
njs_int_t njs_object_hash_create(njs_vm_t *vm, njs_flathsh_t *hash,
    const njs_object_prop_init_t *prop, njs_uint_t n);
njs_int_t njs_primitive_prototype_get_proto(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
njs_int_t njs_object_prototype_create(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
njs_value_t *njs_property_prototype_create(njs_vm_t *vm, njs_flathsh_t *hash,
    njs_object_t *prototype);
njs_int_t njs_object_prototype_proto(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
njs_int_t njs_object_prototype_create_constructor(njs_vm_t *vm,
    njs_object_prop_t *prop, uint32_t unused, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
njs_value_t *njs_property_constructor_set(njs_vm_t *vm, njs_flathsh_t *hash,
    njs_value_t *constructor);
njs_int_t njs_object_to_string(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *retval);
njs_int_t njs_object_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
njs_int_t njs_object_length(njs_vm_t *vm, njs_value_t *value, int64_t *dst);

njs_int_t njs_prop_private_copy(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *proto);
njs_object_prop_t *njs_object_prop_alloc(njs_vm_t *vm,
    const njs_value_t *value, uint8_t attributes);
njs_int_t njs_object_property(njs_vm_t *vm, njs_object_t *object,
    njs_flathsh_query_t *lhq, njs_value_t *retval);
njs_object_prop_t *njs_object_property_add(njs_vm_t *vm, njs_value_t *object,
    uint32_t atom_id, njs_bool_t replace);
njs_int_t njs_object_prop_define(njs_vm_t *vm, njs_value_t *object,
    uint32_t atom_id, njs_value_t *value, uint32_t flags);
njs_int_t njs_object_prop_descriptor(njs_vm_t *vm, njs_value_t *dest,
    njs_value_t *value, njs_value_t *setval);
njs_int_t njs_object_get_prototype_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
const char *njs_prop_type_string(njs_object_prop_type_t type);
njs_int_t njs_object_prop_init(njs_vm_t *vm, const njs_object_init_t* init,
    njs_object_prop_t *base, uint32_t atom_id, njs_value_t *value,
    njs_value_t *retval);


njs_inline njs_bool_t
njs_is_data_descriptor(njs_object_prop_t *prop)
{
    return prop->writable != NJS_ATTRIBUTE_UNSET
           || (prop->type != NJS_ACCESSOR && njs_is_valid(njs_prop_value(prop)))
           || prop->type == NJS_PROPERTY_HANDLER;

}


njs_inline njs_bool_t
njs_is_accessor_descriptor(njs_object_prop_t *prop)
{
    return prop->type == NJS_ACCESSOR;
}


njs_inline njs_bool_t
njs_is_generic_descriptor(njs_object_prop_t *prop)
{
    return !njs_is_data_descriptor(prop) && !njs_is_accessor_descriptor(prop);
}


njs_inline njs_int_t
njs_primitive_value_to_key(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *src)
{
    switch (src->type) {
    case NJS_NULL:
        njs_atom_to_value(vm, dst, NJS_ATOM_STRING_null);
        return NJS_OK;

    case NJS_UNDEFINED:
        njs_atom_to_value(vm, dst, NJS_ATOM_STRING_undefined);
        return NJS_OK;

    case NJS_BOOLEAN:
        if (njs_is_true(src)) {
            njs_atom_to_value(vm, dst, NJS_ATOM_STRING_true);

        } else {
            njs_atom_to_value(vm, dst, NJS_ATOM_STRING_false);
        }

        return NJS_OK;

    case NJS_NUMBER:
    case NJS_SYMBOL:
    case NJS_STRING:
        *dst = *src;
        return NJS_OK;

    default:
        return NJS_ERROR;
    }
}


njs_inline njs_int_t
njs_value_to_key2(njs_vm_t *vm, njs_value_t *dst, njs_value_t *value,
    njs_bool_t convert)
{
    njs_int_t    ret;
    njs_value_t  primitive;

    if (njs_slow_path(!njs_is_primitive(value))) {
        if (njs_slow_path(njs_is_object_symbol(value))) {
            /* should fail */
            value = njs_object_value(value);

        } else {
            if (convert) {
                ret = njs_value_to_primitive(vm, &primitive, value, 1);

            } else {
                ret = njs_object_to_string(vm, value, &primitive);
            }

            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            value = &primitive;
        }
    }

    return njs_primitive_value_to_key(vm, dst, value);
}


njs_inline njs_int_t
njs_value_to_key(njs_vm_t *vm, njs_value_t *dst, njs_value_t *value)
{
    return njs_value_to_key2(vm, dst, value, 1);
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

    } else if (njs_slow_path(!njs_is_string(key))) {
        ret = njs_value_to_string(vm, key, key);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_string_get(vm, key, str);

    return NJS_OK;
}


njs_inline njs_int_t
njs_atom_string_get(njs_vm_t *vm, uint32_t atom_id, njs_str_t *str)
{
    njs_value_t  value;

    if (njs_atom_to_value(vm, &value, atom_id) != NJS_OK) {
        return NJS_ERROR;
    }

    njs_key_string_get(vm, &value, str);

    return NJS_OK;
}


njs_inline njs_int_t
njs_object_prop_define_val(njs_vm_t *vm, njs_value_t *object, njs_value_t *name,
    njs_value_t *value, unsigned flags)
{
    njs_int_t  ret;

    if (njs_value_atom(name) == NJS_ATOM_STRING_unknown) {
        ret = njs_atom_atomize_key(vm, name);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    if (njs_is_string(name)) {
       flags |= NJS_OBJECT_PROP_IS_STRING;
    }

    return njs_object_prop_define(vm, object, name->atom_id, value, flags);
}


njs_inline njs_int_t
njs_value_create_data_prop(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *name, njs_value_t *setval, uint32_t hash)
{
    return njs_object_prop_define_val(vm, value, name, setval,
                                      NJS_OBJECT_PROP_CREATE
                                      | NJS_OBJECT_PROP_VALUE_ECW);
}


njs_inline njs_int_t
njs_value_create_data_prop_i64(njs_vm_t *vm, njs_value_t *value, int64_t index,
    njs_value_t *setval, uint32_t hash)
{
    njs_value_t  key;

    njs_set_number(&key, index);

    return njs_value_create_data_prop(vm, value, &key, setval, hash);
}


njs_inline njs_int_t
njs_object_length_set(njs_vm_t *vm, njs_value_t *value, int64_t length)
{
    njs_value_t  index;

    njs_value_number_set(&index, length);

    return njs_value_property_set(vm, value, NJS_ATOM_STRING_length, &index);
}


njs_inline njs_int_t
njs_object_string_tag(njs_vm_t *vm, njs_value_t *value, njs_value_t *tag)
{
    njs_int_t  ret;

    ret = njs_value_property(vm, value, NJS_ATOM_SYMBOL_toStringTag, tag);
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
