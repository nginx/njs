
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_OBJECT_H_INCLUDED_
#define _NJS_OBJECT_H_INCLUDED_


typedef enum {
    NJS_PROPERTY = 0,
    NJS_PROPERTY_REF,
    NJS_METHOD,
    NJS_PROPERTY_HANDLER,
    NJS_WHITEOUT,
} njs_object_property_type_t;


/*
 * Attributes are generally used as Boolean values.
 * The UNSET value is used internally only by njs_define_property().
 */
typedef enum {
    NJS_ATTRIBUTE_FALSE = 0,
    NJS_ATTRIBUTE_TRUE = 1,
    NJS_ATTRIBUTE_UNSET,
} njs_object_attribute_t;

typedef struct {
    /* Must be aligned to njs_value_t. */
    njs_value_t                 value;
    njs_value_t                 name;

    njs_object_property_type_t  type:8;          /* 3 bits */

    njs_object_attribute_t      enumerable:8;    /* 2 bits */
    njs_object_attribute_t      writable:8;      /* 2 bits */
    njs_object_attribute_t      configurable:8;  /* 2 bits */
} njs_object_prop_t;


typedef struct {
    nxt_lvlhsh_query_t          lhq;

    /* scratch is used to get the value of an NJS_PROPERTY_HANDLER property. */
    njs_object_prop_t           scratch;

    /* These three fields are used for NJS_EXTERNAL setters. */
    uintptr_t                   ext_data;
    const njs_extern_t          *ext_proto;
    uint32_t                    ext_index;

    njs_value_t                 value;
    njs_object_t                *prototype;
    uint8_t                     query;
    uint8_t                     shared;
    uint8_t                     own;
} njs_property_query_t;


#define njs_property_query_init(pq, _query, _own)                             \
    do {                                                                      \
        (pq)->lhq.key.length = 0;                                             \
        (pq)->lhq.value = NULL;                                               \
        (pq)->query = _query;                                                 \
        (pq)->own = _own;                                                     \
    } while (0)


struct njs_object_init_s {
    nxt_str_t                   name;
    const njs_object_prop_t     *properties;
    nxt_uint_t                  items;
};


njs_object_t *njs_object_alloc(njs_vm_t *vm);
njs_object_t *njs_object_value_copy(njs_vm_t *vm, njs_value_t *value);
njs_object_t *njs_object_value_alloc(njs_vm_t *vm, const njs_value_t *value,
    nxt_uint_t type);
njs_array_t *njs_object_keys_array(njs_vm_t *vm, const njs_value_t *value);
njs_ret_t njs_value_property(njs_vm_t *vm, njs_value_t *value,
    const njs_value_t *property, njs_value_t *retval);
njs_object_prop_t *njs_object_property(njs_vm_t *vm, const njs_object_t *obj,
    nxt_lvlhsh_query_t *lhq);
njs_ret_t njs_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_value_t *object, const njs_value_t *property);
nxt_int_t njs_object_hash_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    const njs_object_prop_t *prop, nxt_uint_t n);
njs_ret_t njs_object_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_object_prop_t *njs_object_prop_alloc(njs_vm_t *vm, const njs_value_t *name,
        const njs_value_t *value, uint8_t attributes);
njs_ret_t njs_primitive_prototype_get_proto(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
njs_ret_t njs_object_prototype_create(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
njs_value_t *njs_property_prototype_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    njs_object_t *prototype);
njs_ret_t njs_object_prototype_get_proto(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
njs_value_t *njs_property_constructor_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    njs_value_t *constructor);
njs_ret_t njs_object_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

njs_ret_t njs_method_private_copy(njs_vm_t *vm, njs_property_query_t *pq);
const char * njs_prop_type_string(njs_object_property_type_t type);

extern const njs_object_init_t  njs_object_constructor_init;
extern const njs_object_init_t  njs_object_prototype_init;

#endif /* _NJS_OBJECT_H_INCLUDED_ */
