
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>


static nxt_int_t njs_object_hash_test(nxt_lvlhsh_query_t *lhq, void *data);
static njs_ret_t njs_object_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_object_t *object,
    const njs_value_t *property);
static njs_ret_t njs_array_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_array_t *array, uint32_t index);
static njs_ret_t njs_string_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_value_t *object, uint32_t index);
static njs_ret_t njs_external_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_value_t *object);
static njs_ret_t njs_external_property_set(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_ret_t njs_external_property_delete(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
static njs_ret_t njs_object_query_prop_handler(njs_property_query_t *pq,
    njs_object_t *object);
static njs_ret_t njs_define_property(njs_vm_t *vm, njs_value_t *object,
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
        return object;
    }

    njs_memory_error(vm);

    return NULL;
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
        return object;
    }

    njs_memory_error(vm);

    return NULL;
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

        return &ov->object;
    }

    njs_memory_error(vm);

    return NULL;
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
            njs_internal_error(vm, "lvlhsh insert failed");
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
        return prop;
    }

    njs_memory_error(vm);

    return NULL;
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
 * ES5.1, 8.12.1: [[GetOwnProperty]], [[GetProperty]].
 * The njs_property_query() returns values
 *   NXT_OK               property has been found in object,
 *     retval of type njs_object_prop_t * is in pq->lhq.value.
 *     in NJS_PROPERTY_QUERY_GET
 *       prop->type is NJS_PROPERTY, NJS_METHOD or NJS_PROPERTY_HANDLER.
 *     in NJS_PROPERTY_QUERY_SET, NJS_PROPERTY_QUERY_DELETE
 *       prop->type is NJS_PROPERTY, NJS_PROPERTY_REF, NJS_METHOD or
 *       NJS_PROPERTY_HANDLER.
 *   NXT_DECLINED         property was not found in object,
 *     if pq->lhq.value != NULL it contains retval of type
 *     njs_object_prop_t * where prop->type is NJS_WHITEOUT
 *   NJS_TRAP             the property trap must be called,
 *   NXT_ERROR            exception has been thrown.
 *
 *   TODO:
 *     Object.create([1,2]).length
 *     Object.defineProperty([1,2], '1', {configurable:false})
 */

njs_ret_t
njs_property_query(njs_vm_t *vm, njs_property_query_t *pq, njs_value_t *object,
    const njs_value_t *property)
{
    uint32_t        index;
    uint32_t        (*hash)(const void *, size_t);
    njs_ret_t       ret;
    njs_object_t    *obj;
    njs_function_t  *function;

    if (nxt_slow_path(!njs_is_primitive(property))) {
        return njs_trap(vm, NJS_TRAP_PROPERTY);
    }

    hash = nxt_djb_hash;

    switch (object->type) {

    case NJS_BOOLEAN:
    case NJS_NUMBER:
        index = njs_primitive_prototype_index(object->type);
        obj = &vm->prototypes[index].object;
        break;

    case NJS_STRING:
        if (nxt_fast_path(!njs_is_null_or_void_or_boolean(property))) {
            index = njs_value_to_index(property);

            if (nxt_fast_path(index < NJS_STRING_MAX_LENGTH)) {
                return njs_string_property_query(vm, pq, object, index);
            }
        }

        obj = &vm->prototypes[NJS_PROTOTYPE_STRING].object;
        break;

    case NJS_OBJECT_STRING:
        if (nxt_fast_path(!njs_is_null_or_void_or_boolean(property))) {
            index = njs_value_to_index(property);

            if (nxt_fast_path(index < NJS_STRING_MAX_LENGTH)) {
                ret = njs_string_property_query(vm, pq,
                                            &object->data.u.object_value->value,
                                            index);

                if (nxt_fast_path(ret != NXT_DECLINED)) {
                    return ret;
                }
            }
        }

        obj = object->data.u.object;
        break;

    case NJS_ARRAY:
        if (nxt_fast_path(!njs_is_null_or_void_or_boolean(property))) {
            index = njs_value_to_index(property);

            if (nxt_fast_path(index < NJS_ARRAY_MAX_LENGTH)) {
                return njs_array_property_query(vm, pq, object->data.u.array,
                                                index);
            }
        }

        /* Fall through. */

    case NJS_OBJECT:
    case NJS_OBJECT_BOOLEAN:
    case NJS_OBJECT_NUMBER:
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
        obj = NULL;
        break;

    case NJS_VOID:
    case NJS_NULL:
    default:
        ret = njs_primitive_value_to_string(vm, &pq->value, property);

        if (nxt_fast_path(ret == NXT_OK)) {
            njs_string_get(&pq->value, &pq->lhq.key);
            njs_type_error(vm, "cannot get property '%.*s' of undefined",
                           (int) pq->lhq.key.length, pq->lhq.key.start);
            return NXT_ERROR;
        }

        njs_type_error(vm, "cannot get property 'unknown' of undefined");

        return NXT_ERROR;
    }

    ret = njs_primitive_value_to_string(vm, &pq->value, property);

    if (nxt_fast_path(ret == NXT_OK)) {

        njs_string_get(&pq->value, &pq->lhq.key);
        pq->lhq.key_hash = hash(pq->lhq.key.start, pq->lhq.key.length);

        if (obj == NULL) {
            return njs_external_property_query(vm, pq, object);
        }

        return njs_object_property_query(vm, pq, obj, property);
    }

    return ret;
}


njs_ret_t
njs_object_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *object, const njs_value_t *property)
{
    uint32_t            index;
    njs_ret_t           ret;
    njs_array_t         *array;
    njs_object_t        *proto;
    njs_object_prop_t   *prop;
    njs_object_value_t  *ov;

    pq->lhq.proto = &njs_object_hash_proto;

    if (pq->query == NJS_PROPERTY_QUERY_SET) {
        ret = njs_object_query_prop_handler(pq, object);
        if (ret == NXT_OK) {
            return ret;
        }
    }

    proto = object;

    do {
        pq->prototype = proto;

        /* length and other shared properties should be Own property */

        if (nxt_fast_path(!pq->own || proto == object)) {
            ret = nxt_lvlhsh_find(&proto->hash, &pq->lhq);

            if (ret == NXT_OK) {
                prop = pq->lhq.value;

                if (prop->type != NJS_WHITEOUT) {
                    pq->shared = 0;

                    return ret;
                }

                goto next;
            }

            if (proto != object && !njs_is_null_or_void_or_boolean(property)) {
                switch (proto->type) {
                case NJS_ARRAY:
                    index = njs_value_to_index(property);
                    if (nxt_fast_path(index < NJS_ARRAY_MAX_LENGTH)) {
                        array = (njs_array_t *) proto;
                        return njs_array_property_query(vm, pq, array, index);
                    }

                    break;

                case NJS_OBJECT_STRING:
                    index = njs_value_to_index(property);
                    if (nxt_fast_path(index < NJS_STRING_MAX_LENGTH)) {
                        ov = (njs_object_value_t *) proto;
                        return njs_string_property_query(vm, pq, &ov->value,
                                                         index);
                    }

                default:
                    break;
                }
            }
        }

        ret = nxt_lvlhsh_find(&proto->shared_hash, &pq->lhq);

        if (ret == NXT_OK) {
            pq->shared = 1;

            return ret;
        }

        if (pq->query > NJS_PROPERTY_QUERY_GET) {
            return NXT_DECLINED;
        }

next:

        proto = proto->__proto__;

    } while (proto != NULL);

    return NXT_DECLINED;
}


static njs_ret_t
njs_array_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_array_t *array, uint32_t index)
{
    uint32_t           size;
    njs_ret_t          ret;
    njs_value_t        *value;
    njs_object_prop_t  *prop;

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

    prop = &pq->scratch;

    if (pq->query == NJS_PROPERTY_QUERY_GET) {
        if (!njs_is_valid(&array->start[index])) {
            return NXT_DECLINED;
        }

        prop->value = array->start[index];
        prop->type = NJS_PROPERTY;

    } else {
        prop->value.data.u.value = &array->start[index];
        prop->type = NJS_PROPERTY_REF;
    }

    prop->configurable = 1;
    prop->enumerable = 1;
    prop->writable = 1;

    pq->lhq.value = prop;

    return NXT_OK;
}


static njs_ret_t
njs_string_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_value_t *object, uint32_t index)
{
    njs_slice_prop_t   slice;
    njs_object_prop_t  *prop;
    njs_string_prop_t  string;

    prop = &pq->scratch;

    slice.start = index;
    slice.length = 1;
    slice.string_length = njs_string_prop(&string, object);

    if (slice.start < slice.string_length) {
        /*
         * A single codepoint string fits in retval
         * so the function cannot fail.
         */
        (void) njs_string_slice(vm, &prop->value, &string, &slice);
        prop->type = NJS_PROPERTY;
        prop->configurable = 0;
        prop->enumerable = 1;
        prop->writable = 0;

        pq->lhq.value = prop;

        if (pq->query != NJS_PROPERTY_QUERY_GET) {
            /* pq->lhq.key is used by njs_vmcode_property_set for TypeError */
            njs_uint32_to_string(&pq->value, index);
            njs_string_get(&pq->value, &pq->lhq.key);
        }

        return NXT_OK;
    }

    return NXT_DECLINED;
}


static njs_ret_t
njs_external_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_value_t *object)
{
    void                *obj;
    njs_ret_t           ret;
    uintptr_t           data;
    njs_object_prop_t   *prop;
    const njs_extern_t  *ext_proto;

    prop = &pq->scratch;

    prop->type = NJS_PROPERTY;
    prop->configurable = 0;
    prop->enumerable = 1;
    prop->writable = 0;

    ext_proto = object->external.proto;

    pq->lhq.proto = &njs_extern_hash_proto;
    ret = nxt_lvlhsh_find(&ext_proto->hash, &pq->lhq);

    if (ret == NXT_OK) {
        ext_proto = pq->lhq.value;

        prop->value.type = NJS_EXTERNAL;
        prop->value.data.truth = 1;
        prop->value.external.proto = ext_proto;
        prop->value.external.index = object->external.index;

        if ((ext_proto->type & NJS_EXTERN_OBJECT) != 0) {
            goto done;
        }

        data = ext_proto->data;

    } else {
        data = (uintptr_t) &pq->lhq.key;
    }

    switch (pq->query) {

    case NJS_PROPERTY_QUERY_GET:
        if (ext_proto->get != NULL) {
            obj = njs_extern_object(vm, object);
            ret = ext_proto->get(vm, &prop->value, obj, data);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }
        }

        break;

    case NJS_PROPERTY_QUERY_SET:
    case NJS_PROPERTY_QUERY_DELETE:

        prop->type = NJS_PROPERTY_HANDLER;
        prop->name = *object;

        if (pq->query == NJS_PROPERTY_QUERY_SET) {
            prop->writable = (ext_proto->set != NULL);
            prop->value.data.u.prop_handler = njs_external_property_set;

        } else {
            prop->configurable = (ext_proto->find != NULL);
            prop->value.data.u.prop_handler = njs_external_property_delete;
        }

        pq->ext_data = data;
        pq->ext_proto = ext_proto;
        pq->ext_index = object->external.index;

        pq->lhq.value = prop;

        vm->stash = (uintptr_t) pq;

        return NXT_OK;
    }

done:

    if (ext_proto->type == NJS_EXTERN_METHOD) {
        prop->value.type = NJS_FUNCTION;
        prop->value.data.u.function = ext_proto->function;
        prop->value.data.truth = 1;
    }

    pq->lhq.value = prop;

    return ret;
}


static njs_ret_t
njs_external_property_set(njs_vm_t *vm, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    void                  *obj;
    njs_ret_t             ret;
    nxt_str_t             s;
    njs_property_query_t  *pq;

    pq = (njs_property_query_t *) vm->stash;

    ret = njs_vm_value_to_ext_string(vm, &s, setval, 0);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    *retval = *setval;

    obj = njs_extern_index(vm, pq->ext_index);

    return pq->ext_proto->set(vm, obj, pq->ext_data, &s);
}


static njs_ret_t
njs_external_property_delete(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *unused, njs_value_t *unused2)
{
    void                  *obj;
    njs_property_query_t  *pq;

    pq = (njs_property_query_t *) vm->stash;

    obj = njs_extern_index(vm, pq->ext_index);

    return pq->ext_proto->find(vm, obj, pq->ext_data, 1);
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
njs_method_private_copy(njs_vm_t *vm, njs_property_query_t *pq)
{
    njs_function_t     *function;
    njs_object_prop_t  *prop, *shared;

    prop = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_prop_t));
    if (nxt_slow_path(prop == NULL)) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    shared = pq->lhq.value;
    *prop = *shared;

    function = njs_function_value_copy(vm, &prop->value);
    if (nxt_slow_path(function == NULL)) {
        return NXT_ERROR;
    }

    pq->lhq.replace = 0;
    pq->lhq.value = prop;
    pq->lhq.pool = vm->mem_cache_pool;

    return nxt_lvlhsh_insert(&pq->prototype->hash, &pq->lhq);
}


njs_ret_t
njs_object_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_uint_t         type;
    njs_object_t       *object;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);
    type = value->type;

    if (njs_is_null_or_void(value)) {

        object = njs_object_alloc(vm);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

        type = NJS_OBJECT;

    } else {

        if (njs_is_object(value)) {
            object = value->data.u.object;

        } else if (njs_is_primitive(value)) {

            /* value->type is the same as prototype offset. */
            object = njs_object_value_alloc(vm, value, type);
            if (nxt_slow_path(object == NULL)) {
                return NXT_ERROR;
            }

            type = njs_object_value_type(type);

        } else {
            njs_type_error(vm, "unexpected constructor argument:%s",
                           njs_type_string(type));

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

    if (njs_is_null_or_void(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NXT_ERROR;
    }

    keys = njs_object_keys_array(vm, value);
    if (keys == NULL) {
        return NXT_ERROR;
    }

    vm->retval.data.u.array = keys;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


njs_array_t *
njs_object_keys_array(njs_vm_t *vm, const njs_value_t *value)
{
    uint32_t           i, n, length, keys_length;
    njs_value_t        *string;
    njs_array_t        *keys, *array;
    nxt_lvlhsh_t       *hash;
    njs_object_prop_t  *prop;
    njs_string_prop_t  string_prop;
    nxt_lvlhsh_each_t  lhe;

    array = NULL;
    length = 0;
    keys_length = 0;

    switch (value->type) {
    case NJS_ARRAY:
        array = value->data.u.array;
        length = array->length;

        for (i = 0; i < length; i++) {
            if (njs_is_valid(&array->start[i])) {
                keys_length++;
            }
        }

        break;

    case NJS_STRING:
    case NJS_OBJECT_STRING:
        if (value->type == NJS_OBJECT_STRING) {
            string = &value->data.u.object_value->value;

        } else {
            string = (njs_value_t *) value;
        }

        length = njs_string_prop(&string_prop, string);
        keys_length += length;
        break;

    default:
        break;
    }

    if (nxt_fast_path(njs_is_object(value))) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
        hash = &value->data.u.object->hash;

    } else {
        hash = NULL;
    }

    if (nxt_fast_path(hash != NULL)) {
        for ( ;; ) {
            prop = nxt_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (prop->type != NJS_WHITEOUT && prop->enumerable) {
                keys_length++;
            }
        }
    }

    keys = njs_array_alloc(vm, keys_length, NJS_ARRAY_SPARE);
    if (nxt_slow_path(keys == NULL)) {
        return NULL;
    }

    n = 0;

    if (array != NULL) {
        for (i = 0; i < length; i++) {
            if (njs_is_valid(&array->start[i])) {
                njs_uint32_to_string(&keys->start[n++], i);
            }
        }

    } else if (length != 0) {
        for (i = 0; i < length; i++) {
            njs_uint32_to_string(&keys->start[n++], i);
        }
    }

    if (nxt_fast_path(hash != NULL)) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

        for ( ;; ) {
            prop = nxt_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            if (prop->type != NJS_WHITEOUT && prop->enumerable) {
                njs_string_copy(&keys->start[n++], &prop->name);
            }
        }
    }

    return keys;
}


static njs_ret_t
njs_object_define_property(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t          ret;
    njs_value_t        *value;
    const njs_value_t  *name, *descriptor;

    if (!njs_is_object(njs_arg(args, nargs, 1))) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(njs_arg(args, nargs, 1)->type));
        return NXT_ERROR;
    }

    value = &args[1];

    if (!value->data.u.object->extensible) {
        njs_type_error(vm, "object is not extensible");
        return NXT_ERROR;
    }

    descriptor = njs_arg(args, nargs, 3);

    if (!njs_is_object(descriptor)) {
        njs_type_error(vm, "descriptor is not an object");
        return NXT_ERROR;
    }

    name = njs_arg(args, nargs, 2);

    ret = njs_define_property(vm, value, name, descriptor->data.u.object);
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
    njs_value_t        *value;
    nxt_lvlhsh_t       *hash;
    nxt_lvlhsh_each_t  lhe;
    njs_object_prop_t  *prop;
    const njs_value_t  *descriptor;

    value = &args[1];

    if (!njs_is_object(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NXT_ERROR;
    }

    if (!value->data.u.object->extensible) {
        njs_type_error(vm, "object is not extensible");
        return NXT_ERROR;
    }

    descriptor = njs_arg(args, nargs, 2);

    if (!njs_is_object(descriptor)) {
        njs_type_error(vm, "descriptor is not an object");
        return NXT_ERROR;
    }

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    hash = &descriptor->data.u.object->hash;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        if (prop->enumerable && njs_is_object(&prop->value)) {
            ret = njs_define_property(vm, value, &prop->name,
                                      prop->value.data.u.object);

            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }
        }
    }

    vm->retval = *value;

    return NXT_OK;
}


static uint8_t
njs_descriptor_attribute(njs_vm_t *vm, const njs_object_t *descriptor,
    nxt_lvlhsh_query_t *pq, nxt_bool_t unset)
{
    njs_object_prop_t  *prop;

    prop = njs_object_property(vm, descriptor, pq);
    if (prop != NULL) {
        return prop->value.data.truth;
    }

    return unset ? NJS_ATTRIBUTE_UNSET : 0;
}


static njs_object_prop_t *
njs_descriptor_prop(njs_vm_t *vm, const njs_value_t *name,
    const njs_object_t *descriptor, nxt_bool_t unset)
{
    const njs_value_t   *value;
    njs_object_prop_t   *prop, *pr;
    nxt_lvlhsh_query_t  pq;

    value = unset ? &njs_value_invalid : &njs_value_void;
    prop = njs_object_prop_alloc(vm, name, value, 0);
    if (nxt_slow_path(prop == NULL)) {
        return NULL;
    }

    pq.key = nxt_string_value("configurable");
    pq.key_hash = NJS_CONFIGURABLE_HASH;
    prop->configurable = njs_descriptor_attribute(vm, descriptor, &pq, unset);

    pq.key = nxt_string_value("enumerable");
    pq.key_hash = NJS_ENUMERABLE_HASH;
    prop->enumerable = njs_descriptor_attribute(vm, descriptor, &pq, unset);

    pq.key = nxt_string_value("writable");
    pq.key_hash = NJS_WRITABABLE_HASH;
    prop->writable = njs_descriptor_attribute(vm, descriptor, &pq, unset);

    pq.key = nxt_string_value("value");
    pq.key_hash = NJS_VALUE_HASH;
    pq.proto = &njs_object_hash_proto;

    pr = njs_object_property(vm, descriptor, &pq);
    if (pr != NULL) {
        prop->value = pr->value;
    }

    return prop;
}


/*
 * ES5.1, 8.12.9: [[DefineOwnProperty]]
 *   Limited support of special descriptors like length and array index
 *   (values can be set, but without property flags support).
 */
static njs_ret_t
njs_define_property(njs_vm_t *vm, njs_value_t *object, const njs_value_t *name,
    const njs_object_t *descriptor)
{
    nxt_int_t             ret;
    nxt_bool_t            unset;
    njs_object_prop_t     *desc, *current;
    njs_property_query_t  pq;

    njs_string_get(name, &pq.lhq.key);
    pq.lhq.key_hash = nxt_djb_hash(pq.lhq.key.start, pq.lhq.key.length);
    pq.lhq.proto = &njs_object_hash_proto;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 0);

    ret = njs_property_query(vm, &pq, object, name);

    if (ret != NXT_OK && ret != NXT_DECLINED) {
        return ret;
    }

    unset = (ret == NXT_OK);
    desc = njs_descriptor_prop(vm, name, descriptor, unset);
    if (nxt_slow_path(desc == NULL)) {
        return NXT_ERROR;
    }

    if (nxt_fast_path(ret == NXT_DECLINED)) {
        if (nxt_slow_path(pq.lhq.value != NULL)) {
            current = pq.lhq.value;

            if (nxt_slow_path(current->type == NJS_WHITEOUT)) {
                /* Previously deleted property.  */
                *current = *desc;
            }

        } else {
            pq.lhq.value = desc;
            pq.lhq.replace = 0;
            pq.lhq.pool = vm->mem_cache_pool;

            ret = nxt_lvlhsh_insert(&object->data.u.object->hash, &pq.lhq);
            if (nxt_slow_path(ret != NXT_OK)) {
                njs_internal_error(vm, "lvlhsh insert failed");
                return NXT_ERROR;
            }
        }

        return NXT_OK;
    }

    /* Updating existing prop. */

    current = pq.lhq.value;

    switch (current->type) {
    case NJS_PROPERTY:
        break;

    case NJS_PROPERTY_REF:
        if (njs_is_valid(&desc->value)) {
            *current->value.data.u.value = desc->value;
        } else {
            *current->value.data.u.value = njs_value_void;
        }

        return NXT_OK;

    case NJS_PROPERTY_HANDLER:
        if (current->writable && njs_is_valid(&desc->value)) {
            ret = current->value.data.u.prop_handler(vm, object, &desc->value,
                                                     &vm->retval);

            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }
        }

        return NXT_OK;

    default:
        njs_internal_error(vm, "unexpected property type '%s' "
                           "while defining property",
                           njs_prop_type_string(current->type));

        return NXT_ERROR;
    }

    if (!current->configurable) {
        if (desc->configurable == NJS_ATTRIBUTE_TRUE) {
            goto exception;
        }

        if (desc->enumerable != NJS_ATTRIBUTE_UNSET
            && current->enumerable != desc->enumerable)
        {
            goto exception;
        }

        if (desc->writable == NJS_ATTRIBUTE_TRUE
            && current->writable == NJS_ATTRIBUTE_FALSE)
        {
            goto exception;
        }

        if (njs_is_valid(&desc->value)
            && current->writable == NJS_ATTRIBUTE_FALSE
            && !njs_values_strict_equal(&desc->value, &current->value))
        {
            goto exception;
        }
    }

    if (desc->configurable != NJS_ATTRIBUTE_UNSET) {
        current->configurable = desc->configurable;
    }

    if (desc->enumerable != NJS_ATTRIBUTE_UNSET) {
        current->enumerable = desc->enumerable;
    }

    if (desc->writable != NJS_ATTRIBUTE_UNSET) {
        current->writable = desc->writable;
    }

    if (njs_is_valid(&desc->value)) {
        current->value = desc->value;
    }

    return NXT_OK;

exception:

    njs_type_error(vm, "Cannot redefine property: '%.*s'",
                   (int) pq.lhq.key.length, pq.lhq.key.start);

    return NXT_ERROR;
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
    nxt_int_t             ret;
    njs_object_t          *descriptor;
    njs_object_prop_t     *pr, *prop;
    const njs_value_t     *value, *property, *setval;
    nxt_lvlhsh_query_t    lhq;
    njs_property_query_t  pq;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_void(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NXT_ERROR;
    }

    property = njs_arg(args, nargs, 2);

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 1);

    ret = njs_property_query(vm, &pq, (njs_value_t *) value, property);

    switch (ret) {
    case NXT_OK:
        break;

    case NXT_DECLINED:
        vm->retval = njs_value_void;
        return NXT_OK;

    case NJS_TRAP:
    case NXT_ERROR:
    default:
        return ret;
    }

    prop = pq.lhq.value;

    switch (prop->type) {
    case NJS_PROPERTY:
        break;

    case NJS_PROPERTY_HANDLER:
        pq.scratch = *prop;
        prop = &pq.scratch;
        ret = prop->value.data.u.prop_handler(vm, (njs_value_t *) value,
                                              NULL, &prop->value);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        break;

    case NJS_METHOD:
        if (pq.shared) {
            ret = njs_method_private_copy(vm, &pq);

            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            prop = pq.lhq.value;
        }

        break;

    default:
        njs_type_error(vm, "unexpected property type: %s",
                       njs_prop_type_string(prop->type));
        return NXT_ERROR;
    }

    descriptor = njs_object_alloc(vm);
    if (nxt_slow_path(descriptor == NULL)) {
        return NXT_ERROR;
    }

    lhq.proto = &njs_object_hash_proto;
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
        njs_internal_error(vm, "lvlhsh insert failed");
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
        njs_internal_error(vm, "lvlhsh insert failed");
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
        njs_internal_error(vm, "lvlhsh insert failed");
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
        njs_internal_error(vm, "lvlhsh insert failed");
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

    njs_internal_error(vm, "lvlhsh insert failed");

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

    njs_internal_error(vm, "lvlhsh insert failed");

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


njs_ret_t
njs_object_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    const njs_value_t  *name;

    static const njs_value_t  *class_name[NJS_TYPE_MAX] = {
        /* Primitives. */
        &njs_object_null_string,
        &njs_object_undefined_string,
        &njs_object_boolean_string,
        &njs_object_number_string,
        &njs_object_string_string,

        &njs_object_data_string,
        &njs_object_exernal_string,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,

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
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_error_string,
        &njs_object_object_string,
    };

    name = class_name[args[0].type];

    if (nxt_fast_path(name != NULL)) {
        vm->retval = *name;

        return NXT_OK;
    }

    njs_internal_error(vm, "Unknown value type");

    return NXT_ERROR;
}


static njs_ret_t
njs_object_prototype_has_own_property(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    nxt_int_t             ret;
    const njs_value_t     *value, *property;
    njs_property_query_t  pq;

    value = njs_arg(args, nargs, 0);

    if (njs_is_null_or_void(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NXT_ERROR;
    }

    property = njs_arg(args, nargs, 1);

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 1);

    ret = njs_property_query(vm, &pq, (njs_value_t *) value, property);

    switch (ret) {
    case NXT_OK:
        vm->retval = njs_value_true;
        return NXT_OK;

    case NXT_DECLINED:
        vm->retval = njs_value_false;
        return NXT_OK;

    case NJS_TRAP:
    case NXT_ERROR:
    default:
        return ret;
    }
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


const char *
njs_prop_type_string(njs_object_property_type_t type)
{
    switch (type) {
    case NJS_PROPERTY_REF:
        return "property_ref";

    case NJS_METHOD:
        return "method";

    case NJS_PROPERTY_HANDLER:
        return "property handler";

    case NJS_WHITEOUT:
        return "whiteout";

    case NJS_PROPERTY:
        return "property";

    default:
        return "unknown";
    }
}
