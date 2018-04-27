
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <stdio.h>
#include <string.h>


static nxt_int_t njs_object_hash_test(nxt_lvlhsh_query_t *lhq, void *data);
static njs_ret_t njs_object_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_value_t *value, njs_object_t *object);
static njs_ret_t njs_array_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_value_t *object, uint32_t index);
static njs_ret_t njs_object_query_prop_handler(njs_property_query_t *pq,
    njs_object_t *object);
static njs_ret_t njs_define_property(njs_vm_t *vm, njs_object_t *object,
    const njs_value_t *name, const njs_object_t *descriptor);


nxt_noinline njs_object_t *
njs_object_alloc(njs_vm_t *vm)
{
    njs_object_t  *object;

    object = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_t));

    if (nxt_fast_path(object != NULL)) {
        nxt_lvlhsh_init(&object->hash);
        nxt_lvlhsh_init(&object->shared_hash);
        object->__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;
        object->type = NJS_OBJECT;
        object->shared = 0;
        object->extensible = 1;
    }

    return object;
}


njs_object_t *
njs_object_value_copy(njs_vm_t *vm, njs_value_t *value)
{
    njs_object_t  *object;

    object = value->data.u.object;

    if (!object->shared) {
        return object;
    }

    object = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_t));

    if (nxt_fast_path(object != NULL)) {
        *object = *value->data.u.object;
        object->__proto__ = &vm->prototypes[NJS_PROTOTYPE_OBJECT].object;
        object->shared = 0;
        value->data.u.object = object;
    }

    return object;
}


nxt_noinline njs_object_t *
njs_object_value_alloc(njs_vm_t *vm, const njs_value_t *value, nxt_uint_t type)
{
    nxt_uint_t          index;
    njs_object_value_t  *ov;

    ov = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_value_t));

    if (nxt_fast_path(ov != NULL)) {
        nxt_lvlhsh_init(&ov->object.hash);
        nxt_lvlhsh_init(&ov->object.shared_hash);
        ov->object.type = njs_object_value_type(type);
        ov->object.shared = 0;
        ov->object.extensible = 1;

        index = njs_primitive_prototype_index(type);
        ov->object.__proto__ = &vm->prototypes[index].object;

        ov->value = *value;
    }

    return &ov->object;
}


nxt_int_t
njs_object_hash_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    const njs_object_prop_t *prop, nxt_uint_t n)
{
    nxt_int_t           ret;
    nxt_lvlhsh_query_t  lhq;

    lhq.replace = 0;
    lhq.proto = &njs_object_hash_proto;
    lhq.pool = vm->mem_cache_pool;

    while (n != 0) {
        njs_string_get(&prop->name, &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
        lhq.value = (void *) prop;

        ret = nxt_lvlhsh_insert(hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        prop++;
        n--;
    }

    return NXT_OK;
}


const nxt_lvlhsh_proto_t  njs_object_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_object_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


static nxt_int_t
njs_object_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    size_t             size;
    u_char             *start;
    njs_object_prop_t  *prop;

    prop = data;

    size = prop->name.short_string.size;

    if (size != NJS_STRING_LONG) {
        if (lhq->key.length != size) {
            return NXT_DECLINED;
        }

        start = prop->name.short_string.start;

    } else {
        if (lhq->key.length != prop->name.long_string.size) {
            return NXT_DECLINED;
        }

        start = prop->name.long_string.data->start;
    }

    if (memcmp(start, lhq->key.start, lhq->key.length) == 0) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


nxt_noinline njs_object_prop_t *
njs_object_prop_alloc(njs_vm_t *vm, const njs_value_t *name,
    const njs_value_t *value, uint8_t attributes)
{
    njs_object_prop_t  *prop;

    prop = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                               sizeof(njs_object_prop_t));

    if (nxt_fast_path(prop != NULL)) {
        /* GC: retain. */
        prop->value = *value;

        /* GC: retain. */
        prop->name = *name;

        prop->type = NJS_PROPERTY;
        prop->enumerable = attributes;
        prop->writable = attributes;
        prop->configurable = attributes;
    }

    return prop;
}


nxt_noinline njs_object_prop_t *
njs_object_property(njs_vm_t *vm, const njs_object_t *object,
    nxt_lvlhsh_query_t *lhq)
{
    nxt_int_t  ret;

    lhq->proto = &njs_object_hash_proto;

    do {
        ret = nxt_lvlhsh_find(&object->hash, lhq);

        if (nxt_fast_path(ret == NXT_OK)) {
            return lhq->value;
        }

        ret = nxt_lvlhsh_find(&object->shared_hash, lhq);

        if (nxt_fast_path(ret == NXT_OK)) {
            return lhq->value;
        }

        object = object->__proto__;

    } while (object != NULL);

    return NULL;
}


/*
 * The njs_property_query() returns values
 *   NXT_OK               property has been found in object,
 *   NXT_DECLINED         property was not found in object,
 *   NJS_PRIMITIVE_VALUE  property operation was applied to a numeric
 *                        or boolean value,
 *   NJS_STRING_VALUE     property operation was applied to a string,
 *   NJS_ARRAY_VALUE      object is array,
 *   NJS_EXTERNAL_VALUE   object is external entity,
 *   NJS_TRAP_PROPERTY    the property trap must be called,
 *   NXT_ERROR            exception has been thrown.
 */

njs_ret_t
njs_property_query(njs_vm_t *vm, njs_property_query_t *pq, njs_value_t *object,
    njs_value_t *property)
{
    uint32_t            index;
    uint32_t            (*hash)(const void *, size_t);
    njs_ret_t           ret;
    njs_object_t        *obj;
    njs_function_t      *function;
    const njs_extern_t  *ext_proto;

    hash = nxt_djb_hash;

    switch (object->type) {

    case NJS_BOOLEAN:
    case NJS_NUMBER:
        if (pq->query != NJS_PROPERTY_QUERY_GET) {
            return NJS_PRIMITIVE_VALUE;
        }

        index = njs_primitive_prototype_index(object->type);
        obj = &vm->prototypes[index].object;
        break;

    case NJS_STRING:
        if (pq->query == NJS_PROPERTY_QUERY_DELETE) {
            return NXT_DECLINED;
        }

        obj = &vm->prototypes[NJS_PROTOTYPE_STRING].object;
        break;

    case NJS_ARRAY:
        if (nxt_fast_path(!njs_is_null_or_void_or_boolean(property))) {

            if (nxt_fast_path(njs_is_primitive(property))) {
                index = njs_value_to_index(property);

                if (nxt_fast_path(index < NJS_ARRAY_MAX_LENGTH)) {
                    return njs_array_property_query(vm, pq, object, index);
                }

            } else {
                return NJS_TRAP_PROPERTY;
            }
        }

        /* Fall through. */

    case NJS_OBJECT:
    case NJS_OBJECT_BOOLEAN:
    case NJS_OBJECT_NUMBER:
    case NJS_OBJECT_STRING:
    case NJS_REGEXP:
    case NJS_DATE:
    case NJS_OBJECT_ERROR:
    case NJS_OBJECT_EVAL_ERROR:
    case NJS_OBJECT_INTERNAL_ERROR:
    case NJS_OBJECT_RANGE_ERROR:
    case NJS_OBJECT_REF_ERROR:
    case NJS_OBJECT_SYNTAX_ERROR:
    case NJS_OBJECT_TYPE_ERROR:
    case NJS_OBJECT_URI_ERROR:
    case NJS_OBJECT_VALUE:
        obj = object->data.u.object;
        break;

    case NJS_FUNCTION:
        function = njs_function_value_copy(vm, object);
        if (nxt_slow_path(function == NULL)) {
            return NXT_ERROR;
        }

        obj = &function->object;
        break;

    case NJS_EXTERNAL:
        ext_proto = object->external.proto;

        if (ext_proto->type == NJS_EXTERN_CASELESS_OBJECT) {
            hash = nxt_djb_hash_lowcase;
        }

        obj = NULL;
        break;

    case NJS_VOID:
    case NJS_NULL:
    default:
        if (nxt_fast_path(njs_is_primitive(property))) {

            ret = njs_primitive_value_to_string(vm, &pq->value, property);

            if (nxt_fast_path(ret == NXT_OK)) {
                njs_string_get(&pq->value, &pq->lhq.key);
                njs_type_error(vm, "cannot get property '%.*s' of undefined",
                               (int) pq->lhq.key.length, pq->lhq.key.start);
                return NXT_ERROR;
            }
        }

        njs_type_error(vm, "cannot get property 'unknown' of undefined", NULL);

        return NXT_ERROR;
    }

    if (nxt_fast_path(njs_is_primitive(property))) {

        ret = njs_primitive_value_to_string(vm, &pq->value, property);

        if (nxt_fast_path(ret == NXT_OK)) {

            njs_string_get(&pq->value, &pq->lhq.key);
            pq->lhq.key_hash = hash(pq->lhq.key.start, pq->lhq.key.length);

            if (obj == NULL) {
                pq->lhq.proto = &njs_extern_hash_proto;

                return NJS_EXTERNAL_VALUE;
            }

            return njs_object_property_query(vm, pq, object, obj);
        }

        return ret;
    }

    return NJS_TRAP_PROPERTY;
}


njs_ret_t
njs_object_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_value_t *value, njs_object_t *object)
{
    njs_ret_t          ret;
    njs_object_prop_t  *prop;

    pq->lhq.proto = &njs_object_hash_proto;

    if (pq->query == NJS_PROPERTY_QUERY_SET) {
        ret = njs_object_query_prop_handler(pq, object);
        if (ret == NXT_OK) {
            return ret;
        }
    }

    do {
        pq->prototype = object;

        ret = nxt_lvlhsh_find(&object->hash, &pq->lhq);

        if (ret == NXT_OK) {
            prop = pq->lhq.value;

            if (prop->type != NJS_WHITEOUT) {
                pq->shared = 0;

                return ret;
            }

            goto next;
        }

        if (pq->query > NJS_PROPERTY_QUERY_IN) {
            /* NXT_DECLINED */
            return ret;
        }

        ret = nxt_lvlhsh_find(&object->shared_hash, &pq->lhq);

        if (ret == NXT_OK) {
            pq->shared = 1;

            if (pq->query == NJS_PROPERTY_QUERY_GET) {
                prop = pq->lhq.value;

                if (prop->type == NJS_PROPERTY_HANDLER) {
                    pq->scratch = *prop;
                    prop = &pq->scratch;
                    ret = prop->value.data.u.prop_handler(vm, value, NULL,
                                                          &prop->value);

                    if (nxt_fast_path(ret == NXT_OK)) {
                        prop->type = NJS_PROPERTY;
                        pq->lhq.value = prop;
                    }
                }
            }

            return ret;
        }

        if (pq->query > NJS_PROPERTY_QUERY_IN) {
            /* NXT_DECLINED */
            return ret;
        }

    next:

        object = object->__proto__;

    } while (object != NULL);

    if (njs_is_string(value)) {
        return NJS_STRING_VALUE;
    }

    /* NXT_DECLINED */

    return ret;
}


static njs_ret_t
njs_array_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_value_t *object, uint32_t index)
{
    uint32_t     size;
    njs_ret_t    ret;
    njs_value_t  *value;
    njs_array_t  *array;

    array = object->data.u.array;

    if (index >= array->length) {
        if (pq->query != NJS_PROPERTY_QUERY_SET) {
            return NXT_DECLINED;
        }

        size = index - array->length;

        ret = njs_array_expand(vm, array, 0, size + 1);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        value = &array->start[array->length];

        while (size != 0) {
            njs_set_invalid(value);
            value++;
            size--;
        }

        array->length = index + 1;
    }

    pq->lhq.value = &array->start[index];

    return NJS_ARRAY_VALUE;
}


static njs_ret_t
njs_object_query_prop_handler(njs_property_query_t *pq, njs_object_t *object)
{
    njs_ret_t          ret;
    njs_object_prop_t  *prop;

    do {
        pq->prototype = object;

        ret = nxt_lvlhsh_find(&object->shared_hash, &pq->lhq);

        if (ret == NXT_OK) {
            prop = pq->lhq.value;

            if (prop->type == NJS_PROPERTY_HANDLER) {
                return NXT_OK;
            }
        }

        object = object->__proto__;

    } while (object != NULL);

    return NXT_DECLINED;
}


njs_ret_t
njs_object_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_uint_t         type;
    njs_object_t       *object;
    const njs_value_t  *value;

    type = NJS_OBJECT;
    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_void(value)) {

        object = njs_object_alloc(vm);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

    } else {

        if (njs_is_object(value)) {
            object = value->data.u.object;

        } else if (njs_is_primitive(value)) {

            /* value->type is the same as prototype offset. */
            object = njs_object_value_alloc(vm, value, value->type);
            if (nxt_slow_path(object == NULL)) {
                return NXT_ERROR;
            }

            type = njs_object_value_type(value->type);

        } else {
            njs_type_error(vm, "unexpected constructor argument:%s",
                           njs_type_string(value->type));

            return NXT_ERROR;
        }
    }

    vm->retval.data.u.object = object;
    vm->retval.type = type;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


/* TODO: properties with attributes. */

static njs_ret_t
njs_object_create(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_object_t       *object;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_object(value) || njs_is_null(value)) {

        object = njs_object_alloc(vm);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

        if (!njs_is_null(value)) {
            /* GC */
            object->__proto__ = value->data.u.object;

        } else {
            object->__proto__ = NULL;
        }

        vm->retval.data.u.object = object;
        vm->retval.type = NJS_OBJECT;
        vm->retval.data.truth = 1;

        return NXT_OK;
    }

    njs_type_error(vm, "prototype may only be an object or null: %s",
                   njs_type_string(value->type));

    return NXT_ERROR;
}


static njs_ret_t
njs_object_keys(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_array_t        *keys;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NXT_ERROR;
    }

    keys = njs_object_keys_array(vm, value);
    if (keys == NULL) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    vm->retval.data.u.array = keys;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    return NXT_OK;
}

njs_array_t*
njs_object_keys_array(njs_vm_t *vm, const njs_value_t *object)
{
    size_t             size;
    uint32_t           i, n, keys_length, array_length;
    njs_value_t        *value;
    njs_array_t        *keys, *array;
    nxt_lvlhsh_t       *hash;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;

    array = NULL;
    keys_length = 0;
    array_length = 0;

    if (njs_is_array(object)) {
        array = object->data.u.array;
        array_length = array->length;

        for (i = 0; i < array_length; i++) {
            if (njs_is_valid(&array->start[i])) {
                keys_length++;
            }
        }
    }

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->data.u.object->hash;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->enumerable) {
            keys_length++;
        }
    }

    keys = njs_array_alloc(vm, keys_length, NJS_ARRAY_SPARE);
    if (nxt_slow_path(keys == NULL)) {
        return NULL;
    }

    n = 0;

    for (i = 0; i < array_length; i++) {
        if (njs_is_valid(&array->start[i])) {
            value = &keys->start[n++];
            /*
             * The maximum array index is 4294967294, so
             * it can be stored as a short string inside value.
             */
            size = snprintf((char *) njs_string_short_start(value),
                            NJS_STRING_SHORT, "%u", i);
            njs_string_short_set(value, size, size);
        }
    }

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->enumerable) {
            njs_string_copy(&keys->start[n++], &prop->name);
        }
    }

    return keys;
}


static njs_ret_t
njs_object_define_property(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t   ret;
    const njs_value_t  *value, *name, *descriptor;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NXT_ERROR;
    }

    if (!value->data.u.object->extensible) {
        njs_type_error(vm, "object is not extensible", NULL);
        return NXT_ERROR;
    }

    descriptor = njs_arg(args, nargs, 3);

    if (!njs_is_object(descriptor)){
        njs_type_error(vm, "descriptor is not an object", NULL);
        return NXT_ERROR;
    }

    name = njs_arg(args, nargs, 2);

    ret = njs_define_property(vm, value->data.u.object, name,
                              descriptor->data.u.object);

    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_object_define_properties(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t          ret;
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    nxt_lvlhsh_each_t  lhe;
    njs_object_prop_t  *prop;
    const njs_value_t  *value, *descriptor;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NXT_ERROR;
    }

    if (!value->data.u.object->extensible) {
        njs_type_error(vm, "object is not extensible", NULL);
        return NXT_ERROR;
    }

    descriptor = njs_arg(args, nargs, 2);

    if (!njs_is_object(descriptor)) {
        njs_type_error(vm, "descriptor is not an object", NULL);
        return NXT_ERROR;
    }

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    object = value->data.u.object;
    hash = &descriptor->data.u.object->hash;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->enumerable && njs_is_object(&prop->value)) {
            ret = njs_define_property(vm, object, &prop->name,
                                      prop->value.data.u.object);

            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }
        }
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_define_property(njs_vm_t *vm, njs_object_t *object, const njs_value_t *name,
    const njs_object_t *descriptor)
{
    nxt_int_t           ret;
    njs_object_prop_t   *prop, *pr;
    nxt_lvlhsh_query_t  lhq, pq;

    njs_string_get(name, &lhq.key);
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_find(&object->hash, &lhq);

    if (ret != NXT_OK) {
        prop = njs_object_prop_alloc(vm, name, &njs_value_void, 0);

        if (nxt_slow_path(prop == NULL)) {
            return NXT_ERROR;
        }

        lhq.value = prop;

    } else {
        prop = lhq.value;
    }

    pq.key = nxt_string_value("value");
    pq.key_hash = NJS_VALUE_HASH;
    pq.proto = &njs_object_hash_proto;

    pr = njs_object_property(vm, descriptor, &pq);

    if (pr != NULL) {
        prop->value = pr->value;
    }

    pq.key = nxt_string_value("configurable");
    pq.key_hash = NJS_CONFIGURABLE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);

    if (pr != NULL) {
        prop->configurable = pr->value.data.truth;
    }

    pq.key = nxt_string_value("enumerable");
    pq.key_hash = NJS_ENUMERABLE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);

    if (pr != NULL) {
        prop->enumerable = pr->value.data.truth;
    }

    pq.key = nxt_string_value("writable");
    pq.key_hash = NJS_WRITABABLE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);

    if (pr != NULL) {
        prop->writable = pr->value.data.truth;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;

    ret = nxt_lvlhsh_insert(&object->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


static const njs_value_t  njs_object_value_string = njs_string("value");
static const njs_value_t  njs_object_configurable_string =
                                                    njs_string("configurable");
static const njs_value_t  njs_object_enumerable_string =
                                                    njs_string("enumerable");
static const njs_value_t  njs_object_writable_string =
                                                    njs_string("writable");


static njs_ret_t
njs_object_get_own_property_descriptor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double                num;
    uint32_t              index;
    nxt_int_t             ret;
    njs_array_t           *array;
    njs_object_t          *descriptor;
    njs_object_prop_t     *pr, *prop, array_prop;
    const njs_value_t     *value, *property, *setval;
    nxt_lvlhsh_query_t    lhq;
    njs_property_query_t  pq;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        if (njs_is_null_or_void(value)) {
            njs_type_error(vm, "cannot convert %s argument to object",
                           njs_type_string(value->type));
            return NXT_ERROR;
        }

        vm->retval = njs_value_void;
        return NXT_OK;
    }

    prop = NULL;
    property = njs_arg(args, nargs, 2);

    if (njs_is_array(value)) {
        array = value->data.u.array;
        num = njs_string_to_index(property);
        index = num;

        if ((double) index == num
            && index < array->length
            && njs_is_valid(&array->start[index]))
        {
            prop = &array_prop;

            array_prop.name = *property;
            array_prop.value = array->start[index];

            array_prop.configurable = 1;
            array_prop.enumerable = 1;
            array_prop.writable = 1;
        }
    }

    lhq.proto = &njs_object_hash_proto;

    if (prop == NULL) {
        pq.query = NJS_PROPERTY_QUERY_GET;
        pq.lhq.key.length = 0;
        pq.lhq.key.start = NULL;

        ret = njs_property_query(vm, &pq, (njs_value_t *) value,
                                 (njs_value_t *) property);

        if (ret != NXT_OK) {
            vm->retval = njs_value_void;
            return NXT_OK;
        }

        prop = pq.lhq.value;
    }

    descriptor = njs_object_alloc(vm);
    if (nxt_slow_path(descriptor == NULL)) {
        return NXT_ERROR;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    lhq.key = nxt_string_value("value");
    lhq.key_hash = NJS_VALUE_HASH;

    pr = njs_object_prop_alloc(vm, &njs_object_value_string, &prop->value, 1);
    if (nxt_slow_path(pr == NULL)) {
        return NXT_ERROR;
    }

    lhq.value = pr;

    ret = nxt_lvlhsh_insert(&descriptor->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    lhq.key = nxt_string_value("configurable");
    lhq.key_hash = NJS_CONFIGURABLE_HASH;

    setval = (prop->configurable == 1) ? &njs_value_true : &njs_value_false;

    pr = njs_object_prop_alloc(vm, &njs_object_configurable_string, setval, 1);
    if (nxt_slow_path(pr == NULL)) {
        return NXT_ERROR;
    }

    lhq.value = pr;

    ret = nxt_lvlhsh_insert(&descriptor->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    lhq.key = nxt_string_value("enumerable");
    lhq.key_hash = NJS_ENUMERABLE_HASH;

    setval = (prop->enumerable == 1) ? &njs_value_true : &njs_value_false;

    pr = njs_object_prop_alloc(vm, &njs_object_enumerable_string, setval, 1);
    if (nxt_slow_path(pr == NULL)) {
        return NXT_ERROR;
    }

    lhq.value = pr;

    ret = nxt_lvlhsh_insert(&descriptor->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    lhq.key = nxt_string_value("writable");
    lhq.key_hash = NJS_WRITABABLE_HASH;

    setval = (prop->writable == 1) ? &njs_value_true : &njs_value_false;

    pr = njs_object_prop_alloc(vm, &njs_object_writable_string, setval, 1);
    if (nxt_slow_path(pr == NULL)) {
        return NXT_ERROR;
    }

    lhq.value = pr;

    ret = nxt_lvlhsh_insert(&descriptor->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    vm->retval.data.u.object = descriptor;
    vm->retval.type = NJS_OBJECT;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static njs_ret_t
njs_object_get_prototype_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_object(value)) {
        njs_object_prototype_get_proto(vm, (njs_value_t *) value, NULL,
                                       &vm->retval);
        return NXT_OK;
    }

    njs_type_error(vm, "cannot convert %s argument to object",
                   njs_type_string(value->type));

    return NXT_ERROR;
}


static njs_ret_t
njs_object_freeze(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = njs_value_void;
        return NXT_OK;
    }

    object = value->data.u.object;
    object->extensible = 0;

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        prop->writable = 0;
        prop->configurable = 0;
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_object_is_frozen(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;
    const njs_value_t  *value, *retval;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = njs_value_true;
        return NXT_OK;
    }

    retval = &njs_value_false;

    object = value->data.u.object;
    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    if (object->extensible) {
        goto done;
    }

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->writable || prop->configurable) {
            goto done;
        }
    }

    retval = &njs_value_true;

done:

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_object_seal(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    const njs_value_t  *value;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = *value;
        return NXT_OK;
    }

    object = value->data.u.object;
    object->extensible = 0;

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        prop->configurable = 0;
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_object_is_sealed(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_lvlhsh_t       *hash;
    njs_object_t       *object;
    njs_object_prop_t  *prop;
    nxt_lvlhsh_each_t  lhe;
    const njs_value_t  *value, *retval;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = njs_value_true;
        return NXT_OK;
    }

    retval = &njs_value_false;

    object = value->data.u.object;
    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &object->hash;

    if (object->extensible) {
        goto done;
    }

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->configurable) {
            goto done;
        }
    }

    retval = &njs_value_true;

done:

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_object_prevent_extensions(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = *value;
        return NXT_OK;
    }

    args[1].data.u.object->extensible = 0;

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_object_is_extensible(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value, *retval;

    value = njs_arg(args, nargs, 1);

    if (!njs_is_object(value)) {
        vm->retval = njs_value_false;
        return NXT_OK;
    }

    retval = value->data.u.object->extensible ? &njs_value_true
                                              : &njs_value_false;

    vm->retval = *retval;

    return NXT_OK;
}


/*
 * The __proto__ property of booleans, numbers and strings primitives,
 * of objects created by Boolean(), Number(), and String() constructors,
 * and of Boolean.prototype, Number.prototype, and String.prototype objects.
 */

njs_ret_t
njs_primitive_prototype_get_proto(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    nxt_uint_t    index;
    njs_object_t  *proto;

    /*
     * The __proto__ getters reside in object prototypes of primitive types
     * and have to return different results for primitive type and for objects.
     */
    if (njs_is_object(value)) {
        proto = value->data.u.object->__proto__;

    } else {
        index = njs_primitive_prototype_index(value->type);
        proto = &vm->prototypes[index].object;
    }

    retval->data.u.object = proto;
    retval->type = proto->type;
    retval->data.truth = 1;

    return NXT_OK;
}


/*
 * The "prototype" property of Object(), Array() and other functions is
 * created on demand in the functions' private hash by the "prototype"
 * getter.  The properties are set to appropriate prototype.
 */

njs_ret_t
njs_object_prototype_create(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    int32_t            index;
    njs_function_t     *function;
    const njs_value_t  *proto;

    proto = NULL;
    function = value->data.u.function;
    index = function - vm->constructors;

    if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
        proto = njs_property_prototype_create(vm, &function->object.hash,
                                              &vm->prototypes[index].object);
    }

    if (proto == NULL) {
        proto = &njs_value_void;
    }

    *retval = *proto;

    return NXT_OK;
}


njs_value_t *
njs_property_prototype_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    njs_object_t *prototype)
{
    nxt_int_t                  ret;
    njs_object_prop_t          *prop;
    nxt_lvlhsh_query_t         lhq;

    static const njs_value_t   prototype_string = njs_string("prototype");

    prop = njs_object_prop_alloc(vm, &prototype_string, &njs_value_void, 0);
    if (nxt_slow_path(prop == NULL)) {
        return NULL;
    }

    /* GC */

    prop->value.data.u.object = prototype;
    prop->value.type = prototype->type;
    prop->value.data.truth = 1;

    lhq.value = prop;
    lhq.key_hash = NJS_PROTOTYPE_HASH;
    lhq.key = nxt_string_value("prototype");
    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return &prop->value;
    }

    /* Memory allocation or NXT_DECLINED error. */
    njs_internal_error(vm, NULL, NULL);

    return NULL;
}


static const njs_object_prop_t  njs_object_constructor_properties[] =
{
    /* Object.name == "Object". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Object"),
    },

    /* Object.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* Object.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    /* Object.create(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("create"),
        .value = njs_native_function(njs_object_create, 0, 0),
    },

    /* Object.keys(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("keys"),
        .value = njs_native_function(njs_object_keys, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.defineProperty(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("defineProperty"),
        .value = njs_native_function(njs_object_define_property, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG,
                                     NJS_STRING_ARG, NJS_OBJECT_ARG),
    },

    /* Object.defineProperties(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("defineProperties"),
        .value = njs_native_function(njs_object_define_properties, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG,
                                     NJS_OBJECT_ARG),
    },

    /* Object.getOwnPropertyDescriptor(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("getOwnPropertyDescriptor"),
        .value = njs_native_function(njs_object_get_own_property_descriptor, 0,
                                     NJS_SKIP_ARG, NJS_SKIP_ARG,
                                     NJS_STRING_ARG),
    },

    /* Object.getPrototypeOf(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("getPrototypeOf"),
        .value = njs_native_function(njs_object_get_prototype_of, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.freeze(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("freeze"),
        .value = njs_native_function(njs_object_freeze, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.isFrozen(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isFrozen"),
        .value = njs_native_function(njs_object_is_frozen, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.seal(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("seal"),
        .value = njs_native_function(njs_object_seal, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.isSealed(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isSealed"),
        .value = njs_native_function(njs_object_is_sealed, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.preventExtensions(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("preventExtensions"),
        .value = njs_native_function(njs_object_prevent_extensions, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },

    /* Object.isExtensible(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isExtensible"),
        .value = njs_native_function(njs_object_is_extensible, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
    },
};


const njs_object_init_t  njs_object_constructor_init = {
    nxt_string("Object"),
    njs_object_constructor_properties,
    nxt_nitems(njs_object_constructor_properties),
};


njs_ret_t
njs_object_prototype_get_proto(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    njs_object_t  *proto;

    proto = value->data.u.object->__proto__;

    if (nxt_fast_path(proto != NULL)) {
        retval->data.u.object = proto;
        retval->type = proto->type;
        retval->data.truth = 1;

    } else {
        *retval = njs_value_null;
    }

    return NXT_OK;
}


/*
 * The "constructor" property of Object(), Array() and other functions
 * prototypes is created on demand in the prototypes' private hash by the
 * "constructor" getter.  The properties are set to appropriate function.
 */

static njs_ret_t
njs_object_prototype_create_constructor(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    int32_t                 index;
    njs_value_t             *cons;
    njs_object_t            *object;
    njs_object_prototype_t  *prototype;

    if (njs_is_object(value)) {
        object = value->data.u.object;

        do {
            prototype = (njs_object_prototype_t *) object;
            index = prototype - vm->prototypes;

            if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
                goto found;
            }

            object = object->__proto__;

        } while (object != NULL);

        nxt_thread_log_alert("prototype not found");

        return NXT_ERROR;

    } else {
        index = njs_primitive_prototype_index(value->type);
        prototype = &vm->prototypes[index];
    }

found:

    cons = njs_property_constructor_create(vm, &prototype->object.hash,
                                          &vm->scopes[NJS_SCOPE_GLOBAL][index]);
    if (nxt_fast_path(cons != NULL)) {
        *retval = *cons;
        return NXT_OK;
    }

    return NXT_ERROR;
}


njs_value_t *
njs_property_constructor_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    njs_value_t *constructor)
{
    nxt_int_t                 ret;
    njs_object_prop_t         *prop;
    nxt_lvlhsh_query_t        lhq;

    static const njs_value_t  constructor_string = njs_string("constructor");

    prop = njs_object_prop_alloc(vm, &constructor_string, constructor, 1);
    if (nxt_slow_path(prop == NULL)) {
        return NULL;
    }

    /* GC */

    prop->value = *constructor;
    prop->enumerable = 0;

    lhq.value = prop;
    lhq.key_hash = NJS_CONSTRUCTOR_HASH;
    lhq.key = nxt_string_value("constructor");
    lhq.replace = 0;
    lhq.pool = vm->mem_cache_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return &prop->value;
    }

    /* Memory allocation or NXT_DECLINED error. */
    njs_internal_error(vm, NULL, NULL);

    return NULL;
}


static njs_ret_t
njs_object_prototype_value_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    vm->retval = args[0];

    return NXT_OK;
}


static const njs_value_t  njs_object_null_string = njs_string("[object Null]");
static const njs_value_t  njs_object_undefined_string =
                                     njs_long_string("[object Undefined]");
static const njs_value_t  njs_object_boolean_string =
                                     njs_long_string("[object Boolean]");
static const njs_value_t  njs_object_number_string =
                                     njs_long_string("[object Number]");
static const njs_value_t  njs_object_string_string =
                                     njs_long_string("[object String]");
static const njs_value_t  njs_object_data_string =
                                     njs_string("[object Data]");
static const njs_value_t  njs_object_exernal_string =
                                     njs_long_string("[object External]");
static const njs_value_t  njs_object_object_string =
                                     njs_long_string("[object Object]");
static const njs_value_t  njs_object_array_string =
                                     njs_string("[object Array]");
static const njs_value_t  njs_object_function_string =
                                     njs_long_string("[object Function]");
static const njs_value_t  njs_object_regexp_string =
                                     njs_long_string("[object RegExp]");
static const njs_value_t  njs_object_date_string = njs_string("[object Date]");
static const njs_value_t  njs_object_error_string =
                                     njs_string("[object Error]");
static const njs_value_t  njs_object_eval_error_string =
                                     njs_long_string("[object EvalError]");
static const njs_value_t  njs_object_internal_error_string =
                                     njs_long_string("[object InternalError]");
static const njs_value_t  njs_object_range_error_string =
                                     njs_long_string("[object RangeError]");
static const njs_value_t  njs_object_ref_error_string =
                                     njs_long_string("[object RefError]");
static const njs_value_t  njs_object_syntax_error_string =
                                     njs_long_string("[object SyntaxError]");
static const njs_value_t  njs_object_type_error_string =
                                     njs_long_string("[object TypeError]");
static const njs_value_t  njs_object_uri_error_string =
                                     njs_long_string("[object URIError]");
static const njs_value_t  njs_object_object_value_string =
                                     njs_long_string("[object ObjectValue]");



njs_ret_t
njs_object_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    int32_t                 index;
    njs_object_t            *object;
    const njs_value_t       *value;
    njs_object_prototype_t  *prototype;

    static const njs_value_t  *class_name[] = {
        /* Primitives. */
        &njs_object_null_string,
        &njs_object_undefined_string,
        &njs_object_boolean_string,
        &njs_object_number_string,
        &njs_object_string_string,

        &njs_object_data_string,
        &njs_object_exernal_string,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,
        &njs_string_empty,

        /* Objects. */
        &njs_object_object_string,
        &njs_object_array_string,
        &njs_object_boolean_string,
        &njs_object_number_string,
        &njs_object_string_string,
        &njs_object_function_string,
        &njs_object_regexp_string,
        &njs_object_date_string,
        &njs_object_error_string,
        &njs_object_eval_error_string,
        &njs_object_internal_error_string,
        &njs_object_range_error_string,
        &njs_object_ref_error_string,
        &njs_object_syntax_error_string,
        &njs_object_type_error_string,
        &njs_object_uri_error_string,
        &njs_object_object_value_string,
    };

    value = &args[0];
    index = value->type;

    if (njs_is_object(value)) {
        object = value->data.u.object;

        do {
            prototype = (njs_object_prototype_t *) object;
            index = prototype - vm->prototypes;

            if (index >= 0 && index < NJS_PROTOTYPE_MAX) {
                index += NJS_OBJECT;
                goto found;
            }

            object = object->__proto__;

        } while (object != NULL);

        nxt_thread_log_alert("prototype not found");

        return NXT_ERROR;
    }

found:

    vm->retval = *class_name[index];

    return NXT_OK;
}


static njs_ret_t
njs_object_prototype_has_own_property(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t            index;
    nxt_int_t           ret;
    njs_array_t         *array;
    const njs_value_t   *value, *prop, *retval;
    nxt_lvlhsh_query_t  lhq;

    retval = &njs_value_false;
    value = &args[0];

    if (njs_is_object(value)) {

        prop = njs_arg(args, nargs, 1);

        if (njs_is_array(value)) {
            array = value->data.u.array;
            index = njs_string_to_index(prop);

            if (index < array->length && njs_is_valid(&array->start[index])) {
                retval = &njs_value_true;
                goto done;
            }
        }

        njs_string_get(prop, &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
        lhq.proto = &njs_object_hash_proto;

        ret = nxt_lvlhsh_find(&value->data.u.object->hash, &lhq);

        if (ret == NXT_OK) {
            retval = &njs_value_true;
        }
    }

done:

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_object_prototype_is_prototype_of(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_object_t       *object, *proto;
    const njs_value_t  *prototype, *value, *retval;

    retval = &njs_value_false;
    prototype = &args[0];
    value = njs_arg(args, nargs, 1);

    if (njs_is_object(prototype) && njs_is_object(value)) {
        proto = prototype->data.u.object;
        object = value->data.u.object;

        do {
            object = object->__proto__;

            if (object == proto) {
                retval = &njs_value_true;
                break;
            }

        } while (object != NULL);
    }

    vm->retval = *retval;

    return NXT_OK;
}


static const njs_object_prop_t  njs_object_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("__proto__"),
        .value = njs_prop_handler(njs_object_prototype_get_proto),
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_object_prototype_value_of, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_object_prototype_to_string, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("hasOwnProperty"),
        .value = njs_native_function(njs_object_prototype_has_own_property, 0,
                                     NJS_OBJECT_ARG, NJS_STRING_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("isPrototypeOf"),
        .value = njs_native_function(njs_object_prototype_is_prototype_of, 0,
                                     NJS_OBJECT_ARG, NJS_OBJECT_ARG),
    },
};


const njs_object_init_t  njs_object_prototype_init = {
    nxt_string("Object"),
    njs_object_prototype_properties,
    nxt_nitems(njs_object_prototype_properties),
};
