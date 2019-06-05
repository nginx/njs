
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
static njs_ret_t njs_define_property(njs_vm_t *vm, njs_value_t *object,
    const njs_value_t *name, const njs_object_t *descriptor);

static njs_object_prop_t *njs_object_exist_in_proto(const njs_object_t *begin,
    const njs_object_t *end, nxt_lvlhsh_query_t *lhq);
static uint32_t njs_object_enumerate_array_length(const njs_object_t *object);
static uint32_t njs_object_enumerate_string_length(const njs_object_t *object);
static uint32_t njs_object_enumerate_object_length(const njs_object_t *object,
    nxt_bool_t all);
static uint32_t njs_object_own_enumerate_object_length(
    const njs_object_t *object, const njs_object_t *parent, nxt_bool_t all);
static njs_ret_t njs_object_enumerate_array(njs_vm_t *vm,
    const njs_array_t *array, njs_array_t *items, njs_object_enum_t kind);
static njs_ret_t njs_object_enumerate_string(njs_vm_t *vm,
    const njs_value_t *value, njs_array_t *items, njs_object_enum_t kind);
static njs_ret_t njs_object_enumerate_object(njs_vm_t *vm,
    const njs_object_t *object, njs_array_t *items, njs_object_enum_t kind,
    nxt_bool_t all);
static njs_ret_t njs_object_own_enumerate_object(njs_vm_t *vm,
    const njs_object_t *object, const njs_object_t *parent, njs_array_t *items,
    njs_object_enum_t kind, nxt_bool_t all);


nxt_noinline njs_object_t *
njs_object_alloc(njs_vm_t *vm)
{
    njs_object_t  *object;

    object = nxt_mp_alloc(vm->mem_pool, sizeof(njs_object_t));

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

    object = nxt_mp_alloc(vm->mem_pool, sizeof(njs_object_t));

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

    ov = nxt_mp_alloc(vm->mem_pool, sizeof(njs_object_value_t));

    if (nxt_fast_path(ov != NULL)) {
        nxt_lvlhsh_init(&ov->object.hash);

        if (type == NJS_STRING) {
            ov->object.shared_hash = vm->shared->string_instance_hash;

        } else {
            nxt_lvlhsh_init(&ov->object.shared_hash);
        }

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
    lhq.pool = vm->mem_pool;

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

    prop = nxt_mp_align(vm->mem_pool, sizeof(njs_value_t),
                        sizeof(njs_object_prop_t));

    if (nxt_fast_path(prop != NULL)) {
        /* GC: retain. */
        prop->value = *value;

        /* GC: retain. */
        prop->name = *name;

        prop->type = NJS_PROPERTY;
        prop->writable = attributes;
        prop->enumerable = attributes;
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
        if (nxt_fast_path(!njs_is_null_or_undefined_or_boolean(property))) {
            index = njs_value_to_index(property);

            if (nxt_fast_path(index < NJS_STRING_MAX_LENGTH)) {
                return njs_string_property_query(vm, pq, object, index);
            }
        }

        obj = &vm->string_object;
        break;

    case NJS_OBJECT:
    case NJS_ARRAY:
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
        obj = NULL;
        break;

    case NJS_UNDEFINED:
    case NJS_NULL:
    default:
        ret = njs_primitive_value_to_string(vm, &pq->value, property);

        if (nxt_fast_path(ret == NXT_OK)) {
            njs_string_get(&pq->value, &pq->lhq.key);
            njs_type_error(vm, "cannot get property \"%V\" of undefined",
                           &pq->lhq.key);
            return NXT_ERROR;
        }

        njs_type_error(vm, "cannot get property \"unknown\" of undefined");

        return NXT_ERROR;
    }

    ret = njs_primitive_value_to_string(vm, &pq->value, property);

    if (nxt_fast_path(ret == NXT_OK)) {

        njs_string_get(&pq->value, &pq->lhq.key);
        pq->lhq.key_hash = hash(pq->lhq.key.start, pq->lhq.key.length);

        if (obj == NULL) {
            pq->own = 1;
            return njs_external_property_query(vm, pq, object);
        }

        return njs_object_property_query(vm, pq, obj, property);
    }

    return ret;
}


static njs_ret_t
njs_object_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *object, const njs_value_t *property)
{
    uint32_t            index;
    njs_ret_t           ret;
    nxt_bool_t          own;
    njs_array_t         *array;
    njs_object_t        *proto;
    njs_object_prop_t   *prop;
    njs_object_value_t  *ov;

    pq->lhq.proto = &njs_object_hash_proto;

    own = pq->own;
    pq->own = 1;

    proto = object;

    do {
        pq->prototype = proto;

        if (!njs_is_null_or_undefined_or_boolean(property)) {
            switch (proto->type) {
            case NJS_ARRAY:
                index = njs_value_to_index(property);
                if (nxt_fast_path(index < NJS_ARRAY_MAX_INDEX)) {
                    array = (njs_array_t *) proto;
                    return njs_array_property_query(vm, pq, array, index);
                }

                break;

            case NJS_OBJECT_STRING:
                index = njs_value_to_index(property);
                if (nxt_fast_path(index < NJS_STRING_MAX_LENGTH)) {
                    ov = (njs_object_value_t *) proto;
                    ret = njs_string_property_query(vm, pq, &ov->value, index);

                    if (nxt_fast_path(ret != NXT_DECLINED)) {
                        return ret;
                    }
                }

            default:
                break;
            }
        }

        ret = nxt_lvlhsh_find(&proto->hash, &pq->lhq);

        if (ret == NXT_OK) {
            prop = pq->lhq.value;

            if (prop->type != NJS_WHITEOUT) {
                return ret;
            }

            if (pq->own) {
                pq->own_whiteout = prop;
            }

        } else {
            ret = nxt_lvlhsh_find(&proto->shared_hash, &pq->lhq);

            if (ret == NXT_OK) {
                pq->shared = 1;

                return ret;
            }
        }

        if (own) {
            return NXT_DECLINED;
        }

        pq->own = 0;
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

    prop->writable = 1;
    prop->enumerable = 1;
    prop->configurable = 1;

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
        prop->writable = 0;
        prop->enumerable = 1;
        prop->configurable = 0;

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
    prop->writable = 0;
    prop->enumerable = 1;
    prop->configurable = 0;

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

    if (!njs_is_null_or_undefined(setval)) {
        ret = njs_vm_value_to_ext_string(vm, &s, setval, 0);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

    } else {
        s = nxt_string_value("");
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


njs_ret_t
njs_prop_private_copy(njs_vm_t *vm, njs_property_query_t *pq)
{
    nxt_int_t           ret;
    njs_function_t      *function;
    njs_object_prop_t   *prop, *shared, *name;
    nxt_lvlhsh_query_t  lhq;

    static const njs_value_t  name_string = njs_string("name");

    prop = nxt_mp_align(vm->mem_pool, sizeof(njs_value_t),
                        sizeof(njs_object_prop_t));
    if (nxt_slow_path(prop == NULL)) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    shared = pq->lhq.value;
    *prop = *shared;

    pq->lhq.replace = 0;
    pq->lhq.value = prop;
    pq->lhq.pool = vm->mem_pool;

    ret = nxt_lvlhsh_insert(&pq->prototype->hash, &pq->lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NXT_ERROR;
    }

    if (!njs_is_function(&prop->value)) {
        return NXT_OK;
    }

    function = njs_function_value_copy(vm, &prop->value);
    if (nxt_slow_path(function == NULL)) {
        return NXT_ERROR;
    }

    if (function->ctor) {
        function->object.shared_hash = vm->shared->function_instance_hash;

    } else {
        function->object.shared_hash = vm->shared->arrow_instance_hash;
    }

    name = njs_object_prop_alloc(vm, &name_string, &prop->name, 0);
    if (nxt_slow_path(name == NULL)) {
        return NXT_ERROR;
    }

    name->configurable = 1;

    lhq.key_hash = NJS_NAME_HASH;
    lhq.key = nxt_string_value("name");
    lhq.replace = 0;
    lhq.value = name;
    lhq.proto = &njs_object_hash_proto;
    lhq.pool = vm->mem_pool;

    ret = nxt_lvlhsh_insert(&function->object.hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NXT_ERROR;
    }

    return NXT_OK;
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

    if (njs_is_null_or_undefined(value)) {

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

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NXT_ERROR;
    }

    keys = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS, 0);
    if (keys == NULL) {
        return NXT_ERROR;
    }

    vm->retval.data.u.array = keys;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static njs_ret_t
njs_object_values(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
 {
    njs_array_t        *array;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NXT_ERROR;
    }

    array = njs_value_own_enumerate(vm, value, NJS_ENUM_VALUES, 0);
    if (array == NULL) {
        return NXT_ERROR;
    }

    vm->retval.data.u.array = array;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static njs_ret_t
njs_object_entries(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
 {
    njs_array_t        *array;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NXT_ERROR;
    }

    array = njs_value_own_enumerate(vm, value, NJS_ENUM_BOTH, 0);
    if (array == NULL) {
        return NXT_ERROR;
    }

    vm->retval.data.u.array = array;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static njs_object_prop_t *
njs_object_exist_in_proto(const njs_object_t *object, const njs_object_t *end,
    nxt_lvlhsh_query_t *lhq)
{
    nxt_int_t          ret;
    njs_object_prop_t  *prop;

    lhq->proto = &njs_object_hash_proto;

    while (object != end) {
        ret = nxt_lvlhsh_find(&object->hash, lhq);

        if (nxt_fast_path(ret == NXT_OK)) {
            prop = lhq->value;

            if (prop->type == NJS_WHITEOUT) {
                goto next;
            }

            return lhq->value;
        }

        ret = nxt_lvlhsh_find(&object->shared_hash, lhq);

        if (nxt_fast_path(ret == NXT_OK)) {
            return lhq->value;
        }

next:

        object = object->__proto__;
    }

    return NULL;
}


nxt_inline uint32_t
njs_object_enumerate_length(const njs_object_t *object, nxt_bool_t all)
{
    uint32_t  length;

    length = njs_object_enumerate_object_length(object, all);

    switch (object->type) {
    case NJS_ARRAY:
        length += njs_object_enumerate_array_length(object);
        break;

    case NJS_OBJECT_STRING:
        length += njs_object_enumerate_string_length(object);
        break;

    default:
        break;
    }

    return length;
}


nxt_inline uint32_t
njs_object_own_enumerate_length(const njs_object_t *object,
    const njs_object_t *parent, nxt_bool_t all)
{
    uint32_t  length;

    length = njs_object_own_enumerate_object_length(object, parent, all);

    switch (object->type) {
    case NJS_ARRAY:
        length += njs_object_enumerate_array_length(object);
        break;

    case NJS_OBJECT_STRING:
        length += njs_object_enumerate_string_length(object);
        break;

    default:
        break;
    }

    return length;
}


nxt_inline njs_ret_t
njs_object_enumerate_value(njs_vm_t *vm, const njs_object_t *object,
    njs_array_t *items, njs_object_enum_t kind, nxt_bool_t all)
{
    njs_ret_t           ret;
    njs_object_value_t  *obj_val;

    switch (object->type) {
    case NJS_ARRAY:
        ret = njs_object_enumerate_array(vm, (njs_array_t *) object, items,
                                         kind);
        break;

    case NJS_OBJECT_STRING:
        obj_val = (njs_object_value_t *) object;

        ret = njs_object_enumerate_string(vm, &obj_val->value, items, kind);
        break;

    default:
        goto object;
    }

    if (nxt_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

object:

    ret = njs_object_enumerate_object(vm, object, items, kind, all);
    if (nxt_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


nxt_inline njs_ret_t
njs_object_own_enumerate_value(njs_vm_t *vm, const njs_object_t *object,
    const njs_object_t *parent, njs_array_t *items, njs_object_enum_t kind,
    nxt_bool_t all)
{
    njs_ret_t           ret;
    njs_object_value_t  *obj_val;

    switch (object->type) {
    case NJS_ARRAY:
        ret = njs_object_enumerate_array(vm, (njs_array_t *) object, items,
                                         kind);
        break;

    case NJS_OBJECT_STRING:
        obj_val = (njs_object_value_t *) object;

        ret = njs_object_enumerate_string(vm, &obj_val->value, items, kind);
        break;

    default:
        goto object;
    }

    if (nxt_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

object:

    ret = njs_object_own_enumerate_object(vm, object, parent, items, kind, all);
    if (nxt_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_array_t *
njs_object_enumerate(njs_vm_t *vm, const njs_object_t *object,
    njs_object_enum_t kind, nxt_bool_t all)
{
    uint32_t     length;
    njs_ret_t    ret;
    njs_array_t  *items;

    length = njs_object_enumerate_length(object, all);

    items = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
    if (nxt_slow_path(items == NULL)) {
        return NULL;
    }

    ret = njs_object_enumerate_value(vm, object, items, kind, all);
    if (nxt_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    items->start -= items->length;

    return items;
}


njs_array_t *
njs_object_own_enumerate(njs_vm_t *vm, const njs_object_t *object,
    njs_object_enum_t kind, nxt_bool_t all)
{
    uint32_t     length;
    njs_ret_t    ret;
    njs_array_t  *items;

    length = njs_object_own_enumerate_length(object, object, all);

    items = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
    if (nxt_slow_path(items == NULL)) {
        return NULL;
    }

    ret = njs_object_own_enumerate_value(vm, object, object, items, kind, all);
    if (nxt_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    items->start -= items->length;

    return items;
}


static uint32_t
njs_object_enumerate_array_length(const njs_object_t *object)
{
    uint32_t     i, length;
    njs_array_t  *array;

    length = 0;
    array = (njs_array_t *) object;

    for (i = 0; i < array->length; i++) {
        if (njs_is_valid(&array->start[i])) {
            length++;
        }
    }

    return length;
}


static uint32_t
njs_object_enumerate_string_length(const njs_object_t *object)
{
    njs_object_value_t  *obj_val;

    obj_val = (njs_object_value_t *) object;

    return njs_string_length(&obj_val->value);
}


static uint32_t
njs_object_enumerate_object_length(const njs_object_t *object, nxt_bool_t all)
{
    uint32_t            length;
    const njs_object_t  *proto;

    length = njs_object_own_enumerate_object_length(object, object, all);

    proto = object->__proto__;

    while (proto != NULL) {
        length += njs_object_own_enumerate_length(proto, object, all);
        proto = proto->__proto__;
    }

    return length;
}


static uint32_t
njs_object_own_enumerate_object_length(const njs_object_t *object,
    const njs_object_t *parent, nxt_bool_t all)
{
    uint32_t            length;
    nxt_int_t           ret;
    nxt_lvlhsh_each_t   lhe;
    njs_object_prop_t   *prop, *ext_prop;
    nxt_lvlhsh_query_t  lhq;
    const nxt_lvlhsh_t  *hash;

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
    hash = &object->hash;

    length = 0;

    for ( ;; ) {
        prop = nxt_lvlhsh_each(hash, &lhe);

        if (prop == NULL) {
            break;
        }

        lhq.key_hash = lhe.key_hash;
        njs_string_get(&prop->name, &lhq.key);

        ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

        if (ext_prop == NULL && prop->type != NJS_WHITEOUT
            && (prop->enumerable || all))
        {
            length++;
        }
    }

    if (nxt_slow_path(all)) {
        nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
        hash = &object->shared_hash;

        for ( ;; ) {
            prop = nxt_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            lhq.proto = &njs_object_hash_proto;
            ret = nxt_lvlhsh_find(&object->hash, &lhq);

            if (ret != NXT_OK) {
                ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                if (ext_prop == NULL) {
                    length++;
                }
            }
        }
    }

    return length;
}


static njs_ret_t
njs_object_enumerate_array(njs_vm_t *vm, const njs_array_t *array,
    njs_array_t *items, njs_object_enum_t kind)
{
    uint32_t     i;
    njs_value_t  *item;
    njs_array_t  *entry;

    item = items->start;

    switch (kind) {
    case NJS_ENUM_KEYS:
        for (i = 0; i < array->length; i++) {
            if (njs_is_valid(&array->start[i])) {
                njs_uint32_to_string(item++, i);
            }
        }

        break;

    case NJS_ENUM_VALUES:
        for (i = 0; i < array->length; i++) {
            if (njs_is_valid(&array->start[i])) {
                /* GC: retain. */
                *item++ = array->start[i];
            }
        }

        break;

    case NJS_ENUM_BOTH:
        for (i = 0; i < array->length; i++) {
            if (njs_is_valid(&array->start[i])) {

                entry = njs_array_alloc(vm, 2, 0);
                if (nxt_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_uint32_to_string(&entry->start[0], i);

                /* GC: retain. */
                entry->start[1] = array->start[i];

                item->data.u.array = entry;
                item->type = NJS_ARRAY;
                item->data.truth = 1;

                item++;
            }
        }

        break;
    }

    items->start = item;

    return NJS_OK;
}


static njs_ret_t
njs_object_enumerate_string(njs_vm_t *vm, const njs_value_t *value,
    njs_array_t *items, njs_object_enum_t kind)
{
    u_char             *begin;
    uint32_t           i, len, size;
    njs_value_t        *item, *string;
    njs_array_t        *entry;
    const u_char       *src, *end;
    njs_string_prop_t  str_prop;

    item = items->start;
    len = (uint32_t) njs_string_prop(&str_prop, value);

    switch (kind) {
    case NJS_ENUM_KEYS:
        for (i = 0; i < len; i++) {
            njs_uint32_to_string(item++, i);
        }

        break;

    case NJS_ENUM_VALUES:
        if (str_prop.size == (size_t) len) {
            /* Byte or ASCII string. */

            for (i = 0; i < len; i++) {
                begin = njs_string_short_start(item);
                *begin = str_prop.start[i];

                njs_string_short_set(item, 1, 1);

                item++;
            }

        } else {
            /* UTF-8 string. */

            src = str_prop.start;
            end = src + str_prop.size;

            do {
                begin = (u_char *) src;
                nxt_utf8_copy(njs_string_short_start(item), &src, end);
                size = (uint32_t) (src - begin);

                njs_string_short_set(item, size, 1);

                item++;

            } while (src != end);
        }

        break;

    case NJS_ENUM_BOTH:
        if (str_prop.size == (size_t) len) {
            /* Byte or ASCII string. */

            for (i = 0; i < len; i++) {

                entry = njs_array_alloc(vm, 2, 0);
                if (nxt_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_uint32_to_string(&entry->start[0], i);

                string = &entry->start[1];

                begin = njs_string_short_start(string);
                *begin = str_prop.start[i];

                njs_string_short_set(string, 1, 1);

                item->data.u.array = entry;
                item->type = NJS_ARRAY;
                item->data.truth = 1;

                item++;
            }

        } else {
            /* UTF-8 string. */

            src = str_prop.start;
            end = src + str_prop.size;
            i = 0;

            do {
                entry = njs_array_alloc(vm, 2, 0);
                if (nxt_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_uint32_to_string(&entry->start[0], i++);

                string = &entry->start[1];

                begin = (u_char *) src;
                nxt_utf8_copy(njs_string_short_start(string), &src, end);
                size = (uint32_t) (src - begin);

                njs_string_short_set(string, size, 1);

                item->data.u.array = entry;
                item->type = NJS_ARRAY;
                item->data.truth = 1;

                item++;

            } while (src != end);
        }

        break;
    }

    items->start = item;

    return NJS_OK;
}


static njs_ret_t
njs_object_enumerate_object(njs_vm_t *vm, const njs_object_t *object,
    njs_array_t *items, njs_object_enum_t kind, nxt_bool_t all)
{
    njs_ret_t           ret;
    const njs_object_t  *proto;

    ret = njs_object_own_enumerate_object(vm, object, object, items, kind, all);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    proto = object->__proto__;

    while (proto != NULL) {
        ret = njs_object_own_enumerate_value(vm, proto, object, items, kind,
                                             all);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        proto = proto->__proto__;
    }

    return NJS_OK;
}


static njs_ret_t
njs_object_own_enumerate_object(njs_vm_t *vm, const njs_object_t *object,
    const njs_object_t *parent, njs_array_t *items, njs_object_enum_t kind,
    nxt_bool_t all)
{
    nxt_int_t           ret;
    njs_value_t         *item;
    njs_array_t         *entry;
    nxt_lvlhsh_each_t   lhe;
    njs_object_prop_t   *prop, *ext_prop;
    nxt_lvlhsh_query_t  lhq;
    const nxt_lvlhsh_t  *hash;

    nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);

    item = items->start;
    hash = &object->hash;

    switch (kind) {
    case NJS_ENUM_KEYS:
        for ( ;; ) {
            prop = nxt_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && prop->type != NJS_WHITEOUT
                && (prop->enumerable || all))
            {
                njs_string_copy(item++, &prop->name);
            }
        }

        if (nxt_slow_path(all)) {
            nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
            hash = &object->shared_hash;

            for ( ;; ) {
                prop = nxt_lvlhsh_each(hash, &lhe);

                if (prop == NULL) {
                    break;
                }

                lhq.key_hash = lhe.key_hash;
                njs_string_get(&prop->name, &lhq.key);

                lhq.proto = &njs_object_hash_proto;
                ret = nxt_lvlhsh_find(&object->hash, &lhq);

                if (ret != NXT_OK) {
                    ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                    if (ext_prop == NULL) {
                        njs_string_copy(item++, &prop->name);
                    }
                }
            }
        }

        break;

    case NJS_ENUM_VALUES:
        for ( ;; ) {
            prop = nxt_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && prop->type != NJS_WHITEOUT
                && (prop->enumerable || all))
            {
                /* GC: retain. */
                *item++ = prop->value;
            }
        }

        if (nxt_slow_path(all)) {
            nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
            hash = &object->shared_hash;

            for ( ;; ) {
                prop = nxt_lvlhsh_each(hash, &lhe);

                if (prop == NULL) {
                    break;
                }

                lhq.key_hash = lhe.key_hash;
                njs_string_get(&prop->name, &lhq.key);

                lhq.proto = &njs_object_hash_proto;
                ret = nxt_lvlhsh_find(&object->hash, &lhq);

                if (ret != NXT_OK) {
                    ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                    if (ext_prop == NULL) {
                        *item++ = prop->value;
                    }
                }
            }
        }

        break;

    case NJS_ENUM_BOTH:
        for ( ;; ) {
            prop = nxt_lvlhsh_each(hash, &lhe);

            if (prop == NULL) {
                break;
            }

            lhq.key_hash = lhe.key_hash;
            njs_string_get(&prop->name, &lhq.key);

            ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

            if (ext_prop == NULL && prop->type != NJS_WHITEOUT
                && (prop->enumerable || all))
            {
                entry = njs_array_alloc(vm, 2, 0);
                if (nxt_slow_path(entry == NULL)) {
                    return NJS_ERROR;
                }

                njs_string_copy(&entry->start[0], &prop->name);

                /* GC: retain. */
                entry->start[1] = prop->value;

                item->data.u.array = entry;
                item->type = NJS_ARRAY;
                item->data.truth = 1;

                item++;
            }
        }

        if (nxt_slow_path(all)) {
            nxt_lvlhsh_each_init(&lhe, &njs_object_hash_proto);
            hash = &object->shared_hash;

            for ( ;; ) {
                prop = nxt_lvlhsh_each(hash, &lhe);

                if (prop == NULL) {
                    break;
                }

                lhq.key_hash = lhe.key_hash;
                njs_string_get(&prop->name, &lhq.key);

                lhq.proto = &njs_object_hash_proto;
                ret = nxt_lvlhsh_find(&object->hash, &lhq);

                if (ret != NXT_OK) {
                    ext_prop = njs_object_exist_in_proto(parent, object, &lhq);

                    if (ext_prop == NULL) {
                        entry = njs_array_alloc(vm, 2, 0);
                        if (nxt_slow_path(entry == NULL)) {
                            return NJS_ERROR;
                        }

                        njs_string_copy(&entry->start[0], &prop->name);

                        /* GC: retain. */
                        entry->start[1] = prop->value;

                        item->data.u.array = entry;
                        item->type = NJS_ARRAY;
                        item->data.truth = 1;

                        item++;
                    }
                }
            }
        }

        break;
    }

    items->start = item;

    return NJS_OK;
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

    value = njs_argument(args, 1);

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

    if (!njs_is_object(njs_arg(args, nargs, 1))) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(njs_arg(args, nargs, 1)->type));
        return NXT_ERROR;
    }

    value = njs_argument(args, 1);

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


static njs_object_prop_t *
njs_descriptor_prop(njs_vm_t *vm, const njs_value_t *name,
    const njs_object_t *descriptor)
{
    njs_object_prop_t   *prop, *pr;
    nxt_lvlhsh_query_t  pq;

    prop = njs_object_prop_alloc(vm, name, &njs_value_invalid,
                                 NJS_ATTRIBUTE_UNSET);
    if (nxt_slow_path(prop == NULL)) {
        return NULL;
    }

    pq.key = nxt_string_value("value");
    pq.key_hash = NJS_VALUE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);
    if (pr != NULL) {
        prop->value = pr->value;
    }

    pq.key = nxt_string_value("writable");
    pq.key_hash = NJS_WRITABABLE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);
    if (pr != NULL) {
        prop->writable = pr->value.data.truth;
    }

    pq.key = nxt_string_value("enumerable");
    pq.key_hash = NJS_ENUMERABLE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);
    if (pr != NULL) {
        prop->enumerable = pr->value.data.truth;
    }

    pq.key = nxt_string_value("configurable");
    pq.key_hash = NJS_CONFIGURABLE_HASH;

    pr = njs_object_property(vm, descriptor, &pq);
    if (pr != NULL) {
        prop->configurable = pr->value.data.truth;
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
    njs_object_prop_t     *desc, *current;
    njs_property_query_t  pq;

    njs_string_get(name, &pq.lhq.key);
    pq.lhq.key_hash = nxt_djb_hash(pq.lhq.key.start, pq.lhq.key.length);
    pq.lhq.proto = &njs_object_hash_proto;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 1);

    ret = njs_property_query(vm, &pq, object, name);

    if (ret != NXT_OK && ret != NXT_DECLINED) {
        return ret;
    }

    desc = njs_descriptor_prop(vm, name, descriptor);
    if (nxt_slow_path(desc == NULL)) {
        return NXT_ERROR;
    }

    if (nxt_fast_path(ret == NXT_DECLINED)) {

        if (!njs_is_valid(&desc->value)) {
            desc->value = njs_value_undefined;
        }

        if (desc->writable == NJS_ATTRIBUTE_UNSET) {
            desc->writable = 0;
        }

        if (desc->enumerable == NJS_ATTRIBUTE_UNSET) {
            desc->enumerable = 0;
        }

        if (desc->configurable == NJS_ATTRIBUTE_UNSET) {
            desc->configurable = 0;
        }

        if (nxt_slow_path(pq.lhq.value != NULL)) {
            current = pq.lhq.value;

            if (nxt_slow_path(current->type == NJS_WHITEOUT)) {
                /* Previously deleted property.  */
                *current = *desc;
            }

        } else {
            pq.lhq.value = desc;
            pq.lhq.replace = 0;
            pq.lhq.pool = vm->mem_pool;

            ret = nxt_lvlhsh_insert(&object->data.u.object->hash, &pq.lhq);
            if (nxt_slow_path(ret != NXT_OK)) {
                njs_internal_error(vm, "lvlhsh insert failed");
                return NXT_ERROR;
            }
        }

        return NXT_OK;
    }

    /* Updating existing prop. */

    if (nxt_slow_path(pq.shared)) {
        ret = njs_prop_private_copy(vm, &pq);

        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    current = pq.lhq.value;

    switch (current->type) {
    case NJS_PROPERTY:
    case NJS_METHOD:
        break;

    case NJS_PROPERTY_REF:
        if (njs_is_valid(&desc->value)) {
            *current->value.data.u.value = desc->value;
        } else {
            *current->value.data.u.value = njs_value_undefined;
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
        njs_internal_error(vm, "unexpected property type \"%s\" "
                           "while defining property",
                           njs_prop_type_string(current->type));

        return NXT_ERROR;
    }

    if (!current->configurable) {

        if (njs_is_valid(&desc->value)
            && current->writable == NJS_ATTRIBUTE_FALSE
            && !njs_values_strict_equal(&desc->value, &current->value))
        {
            goto exception;
        }

        if (desc->writable == NJS_ATTRIBUTE_TRUE
            && current->writable == NJS_ATTRIBUTE_FALSE)
        {
            goto exception;
        }

        if (desc->enumerable != NJS_ATTRIBUTE_UNSET
            && current->enumerable != desc->enumerable)
        {
            goto exception;
        }

        if (desc->configurable == NJS_ATTRIBUTE_TRUE) {
            goto exception;
        }
    }

    if (njs_is_valid(&desc->value)) {
        current->value = desc->value;
    }

    if (desc->writable != NJS_ATTRIBUTE_UNSET) {
        current->writable = desc->writable;
    }

    if (desc->enumerable != NJS_ATTRIBUTE_UNSET) {
        current->enumerable = desc->enumerable;
    }

    if (desc->configurable != NJS_ATTRIBUTE_UNSET) {
        current->configurable = desc->configurable;
    }

    return NXT_OK;

exception:

    njs_type_error(vm, "Cannot redefine property: \"%V\"", &pq.lhq.key);

    return NXT_ERROR;
}


static const njs_value_t  njs_object_value_string = njs_string("value");
static const njs_value_t  njs_object_writable_string =
                                                    njs_string("writable");
static const njs_value_t  njs_object_enumerable_string =
                                                    njs_string("enumerable");
static const njs_value_t  njs_object_configurable_string =
                                                    njs_string("configurable");


static njs_ret_t
njs_object_property_descriptor(njs_vm_t *vm, njs_value_t *dest,
    const njs_value_t *value, const njs_value_t *property)
{
    nxt_int_t             ret;
    njs_object_t          *descriptor;
    njs_object_prop_t     *pr, *prop;
    const njs_value_t     *setval;
    nxt_lvlhsh_query_t    lhq;
    njs_property_query_t  pq;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 1);

    ret = njs_property_query(vm, &pq, (njs_value_t *) value, property);

    switch (ret) {
    case NXT_OK:
        break;

    case NXT_DECLINED:
        *dest = njs_value_undefined;
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
            ret = njs_prop_private_copy(vm, &pq);

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
    lhq.pool = vm->mem_pool;

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

    dest->data.u.object = descriptor;
    dest->type = NJS_OBJECT;
    dest->data.truth = 1;

    return NXT_OK;
}


static njs_ret_t
njs_object_get_own_property_descriptor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    const njs_value_t  *value, *property;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NXT_ERROR;
    }

    property = njs_arg(args, nargs, 2);

    return njs_object_property_descriptor(vm, &vm->retval, value, property);
}


static njs_ret_t
njs_object_get_own_property_descriptors(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_ret_t           ret;
    uint32_t            i, length;
    njs_array_t         *names;
    njs_value_t         descriptor;
    njs_object_t        *descriptors;
    const njs_value_t   *value, *key;
    njs_object_prop_t   *pr;
    nxt_lvlhsh_query_t  lhq;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NXT_ERROR;
    }

    names = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS, 1);
    if (nxt_slow_path(names == NULL)) {
        return NXT_ERROR;
    }

    length = names->length;

    descriptors = njs_object_alloc(vm);
    if (nxt_slow_path(descriptors == NULL)) {
        return NXT_ERROR;
    }

    lhq.replace = 0;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    for (i = 0; i < length; i++) {
        key = &names->start[i];
        ret = njs_object_property_descriptor(vm, &descriptor, value, key);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        pr = njs_object_prop_alloc(vm, key, &descriptor, 1);
        if (nxt_slow_path(pr == NULL)) {
            return NXT_ERROR;
        }

        njs_string_get(key, &lhq.key);
        lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);
        lhq.value = pr;

        ret = nxt_lvlhsh_insert(&descriptors->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NXT_ERROR;
        }
    }

    vm->retval.data.u.object = descriptors;
    vm->retval.type = NJS_OBJECT;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static njs_ret_t
njs_object_get_own_property_names(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_array_t        *names;
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));

        return NXT_ERROR;
    }

    names = njs_value_own_enumerate(vm, value, NJS_ENUM_KEYS, 1);
    if (names == NULL) {
        return NXT_ERROR;
    }

    vm->retval.data.u.array = names;
    vm->retval.type = NJS_ARRAY;
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
        njs_object_prototype_proto(vm, (njs_value_t *) value, NULL,
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
        vm->retval = njs_value_undefined;
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
        proto = &njs_value_undefined;
    }

    *retval = *proto;

    return NXT_OK;
}


njs_value_t *
njs_property_prototype_create(njs_vm_t *vm, nxt_lvlhsh_t *hash,
    njs_object_t *prototype)
{
    nxt_int_t                 ret;
    njs_object_prop_t         *prop;
    nxt_lvlhsh_query_t        lhq;

    static const njs_value_t  proto_string = njs_string("prototype");

    prop = njs_object_prop_alloc(vm, &proto_string, &njs_value_undefined, 0);
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
    lhq.pool = vm->mem_pool;
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
        .configurable = 1,
    },

    /* Object.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
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
        .writable = 1,
        .configurable = 1,
    },

    /* Object.keys(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("keys"),
        .value = njs_native_function(njs_object_keys, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES8: Object.values(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("values"),
        .value = njs_native_function(njs_object_values, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES8: Object.entries(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("entries"),
        .value = njs_native_function(njs_object_entries, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.defineProperty(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("defineProperty"),
        .value = njs_native_function(njs_object_define_property, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG,
                                     NJS_STRING_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.defineProperties(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("defineProperties"),
        .value = njs_native_function(njs_object_define_properties, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG,
                                     NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.getOwnPropertyDescriptor(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("getOwnPropertyDescriptor"),
        .value = njs_native_function(njs_object_get_own_property_descriptor, 0,
                                     NJS_SKIP_ARG, NJS_SKIP_ARG,
                                     NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.getOwnPropertyDescriptors(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("getOwnPropertyDescriptors"),
        .value = njs_native_function(njs_object_get_own_property_descriptors, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.getOwnPropertyNames(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("getOwnPropertyNames"),
        .value = njs_native_function(njs_object_get_own_property_names, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.getPrototypeOf(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("getPrototypeOf"),
        .value = njs_native_function(njs_object_get_prototype_of, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.freeze(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("freeze"),
        .value = njs_native_function(njs_object_freeze, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.isFrozen(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isFrozen"),
        .value = njs_native_function(njs_object_is_frozen, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.seal(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("seal"),
        .value = njs_native_function(njs_object_seal, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.isSealed(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isSealed"),
        .value = njs_native_function(njs_object_is_sealed, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.preventExtensions(). */
    {
        .type = NJS_METHOD,
        .name = njs_long_string("preventExtensions"),
        .value = njs_native_function(njs_object_prevent_extensions, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* Object.isExtensible(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("isExtensible"),
        .value = njs_native_function(njs_object_is_extensible, 0,
                                     NJS_SKIP_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_object_constructor_init = {
    nxt_string("Object"),
    njs_object_constructor_properties,
    nxt_nitems(njs_object_constructor_properties),
};


/*
 * ES6, 9.1.2: [[SetPrototypeOf]].
 */
static nxt_bool_t
njs_object_set_prototype_of(njs_vm_t *vm, njs_object_t *object,
    const njs_value_t *value)
{
    const njs_object_t *proto;

    proto = njs_is_object(value) ? value->data.u.object->__proto__
                                 : NULL;

    if (nxt_slow_path(object->__proto__ == proto)) {
        return 1;
    }

    if (nxt_slow_path(proto == NULL)) {
        object->__proto__ = NULL;
        return 1;
    }

    do {
        if (proto == object) {
            return 0;
        }

        proto = proto->__proto__;

    } while (proto != NULL);

    object->__proto__ = value->data.u.object;

    return 1;
}


njs_ret_t
njs_object_prototype_proto(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    nxt_bool_t    ret;
    njs_object_t  *proto, *object;

    if (!njs_is_object(value)) {
        *retval = *value;
        return NJS_OK;
    }

    object = value->data.u.object;

    if (setval != NULL) {
        if (njs_is_object(setval) || njs_is_null(setval)) {
            ret = njs_object_set_prototype_of(vm, object, setval);
            if (nxt_slow_path(!ret)) {
                njs_type_error(vm, "Cyclic __proto__ value");
                return NXT_ERROR;
            }
        }

        *retval = njs_value_undefined;

        return NJS_OK;
    }

    proto = object->__proto__;

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

njs_ret_t
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

    if (setval == NULL) {
        setval = &vm->scopes[NJS_SCOPE_GLOBAL][index];
    }

    cons = njs_property_constructor_create(vm, &prototype->object.hash, setval);
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
    lhq.replace = 1;
    lhq.pool = vm->mem_pool;
    lhq.proto = &njs_object_hash_proto;

    ret = nxt_lvlhsh_insert(hash, &lhq);

    if (nxt_fast_path(ret == NXT_OK)) {
        return &prop->value;
    }

    njs_internal_error(vm, "lvlhsh insert/replace failed");

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

    if (njs_is_null_or_undefined(value)) {
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
njs_object_prototype_prop_is_enumerable(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    nxt_int_t             ret;
    const njs_value_t     *value, *property, *retval;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    value = njs_arg(args, nargs, 0);

    if (njs_is_null_or_undefined(value)) {
        njs_type_error(vm, "cannot convert %s argument to object",
                       njs_type_string(value->type));
        return NXT_ERROR;
    }

    property = njs_arg(args, nargs, 1);

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 1);

    ret = njs_property_query(vm, &pq, (njs_value_t *) value, property);

    switch (ret) {
    case NXT_OK:
        prop = pq.lhq.value;
        retval = prop->enumerable ? &njs_value_true : &njs_value_false;
        break;

    case NXT_DECLINED:
        retval = &njs_value_false;
        break;

    case NJS_TRAP:
    case NXT_ERROR:
    default:
        return ret;
    }

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
        .value = njs_prop_handler(njs_object_prototype_proto),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_object_prototype_value_of, 0, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_object_prototype_to_string, 0, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("hasOwnProperty"),
        .value = njs_native_function(njs_object_prototype_has_own_property, 0,
                                     NJS_OBJECT_ARG, NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_long_string("propertyIsEnumerable"),
        .value = njs_native_function(njs_object_prototype_prop_is_enumerable, 0,
                                     NJS_OBJECT_ARG, NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("isPrototypeOf"),
        .value = njs_native_function(njs_object_prototype_is_prototype_of, 0,
                                     NJS_OBJECT_ARG, NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
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
