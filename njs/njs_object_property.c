
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>


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
static njs_object_prop_t *njs_descriptor_prop(njs_vm_t *vm,
    const njs_value_t *name, const njs_object_t *descriptor);


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


/*
 * ES5.1, 8.12.3: [[Get]].
 *   NXT_OK               property has been found in object,
 *      retval will contain the property's value
 *
 *   NXT_DECLINED         property was not found in object
 *   NJS_TRAP             the property trap must be called
 *   NJS_APPLIED          the property getter was applied
 *   NXT_ERROR            exception has been thrown.
 *      retval will contain undefined
 */
njs_ret_t
njs_value_property(njs_vm_t *vm, const njs_value_t *value,
    const njs_value_t *property, njs_value_t *retval, size_t advance)
{
    njs_ret_t             ret;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0);

    ret = njs_property_query(vm, &pq, (njs_value_t *) value, property);

    switch (ret) {

    case NXT_OK:
        prop = pq.lhq.value;

        switch (prop->type) {

        case NJS_METHOD:
            if (pq.shared) {
                ret = njs_prop_private_copy(vm, &pq);

                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                prop = pq.lhq.value;
            }

            /* Fall through. */

        case NJS_PROPERTY:
            if (njs_is_data_descriptor(prop)) {
                *retval = prop->value;
                break;
            }

            if (njs_is_undefined(&prop->getter)) {
                *retval = njs_value_undefined;
                break;
            }

            return njs_function_activate(vm, prop->getter.data.u.function,
                                         value, NULL, 0, (njs_index_t) retval,
                                         advance);

        case NJS_PROPERTY_HANDLER:
            pq.scratch = *prop;
            prop = &pq.scratch;
            ret = prop->value.data.u.prop_handler(vm, (njs_value_t *) value,
                                                  NULL, &prop->value);

            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            *retval = prop->value;
            break;

        default:
            njs_internal_error(vm, "unexpected property type \"%s\" "
                               "while getting",
                               njs_prop_type_string(prop->type));

            return NXT_ERROR;
        }

        break;

    case NXT_DECLINED:
        *retval = njs_value_undefined;

        return NXT_DECLINED;

    case NJS_TRAP:
    case NXT_ERROR:
    default:

        return ret;
    }

    return NXT_OK;
}


/*
 *   NXT_OK               property has been set successfully
 *   NJS_TRAP             the property trap must be called
 *   NJS_APPLIED          the property setter was applied
 *   NXT_ERROR            exception has been thrown.
 */
njs_ret_t
njs_value_property_set(njs_vm_t *vm, njs_value_t *object,
    const njs_value_t *property, njs_value_t *value, size_t advance)
{
    njs_ret_t             ret;
    njs_object_prop_t     *prop, *shared;
    njs_property_query_t  pq;

    if (njs_is_primitive(object)) {
        njs_type_error(vm, "property set on primitive %s type",
                       njs_type_string(object->type));
        return NXT_ERROR;
    }

    shared = NULL;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 0);

    ret = njs_property_query(vm, &pq, object, property);

    switch (ret) {

    case NXT_OK:
        prop = pq.lhq.value;

        if (njs_is_data_descriptor(prop)) {
            if (!prop->writable) {
                njs_type_error(vm,
                             "Cannot assign to read-only property \"%V\" of %s",
                               &pq.lhq.key, njs_type_string(object->type));
                return NXT_ERROR;
            }

        } else if (!njs_is_function(&prop->setter)) {
            njs_type_error(vm,
                     "Cannot set property \"%V\" of %s which has only a getter",
                           &pq.lhq.key, njs_type_string(object->type));
            return NXT_ERROR;
        }

        if (prop->type == NJS_PROPERTY_HANDLER) {
            ret = prop->value.data.u.prop_handler(vm, object, value,
                                                  &vm->retval);
            if (ret != NXT_DECLINED) {
                return ret;
            }
        }

        if (pq.own) {
            switch (prop->type) {
            case NJS_PROPERTY:
            case NJS_METHOD:
                if (nxt_slow_path(pq.shared)) {
                    shared = prop;
                    break;
                }

                if (njs_is_function(&prop->setter)) {
                    return njs_function_activate(vm,
                                                 prop->setter.data.u.function,
                                                 object, value, 1,
                                                 (njs_index_t) &vm->retval,
                                                 advance);
                }

                goto found;

            case NJS_PROPERTY_REF:
                *prop->value.data.u.value = *value;
                return NXT_OK;

            default:
                njs_internal_error(vm, "unexpected property type \"%s\" "
                                   "while setting",
                                   njs_prop_type_string(prop->type));

                return NXT_ERROR;
            }

            break;
        }

        /* Fall through. */

    case NXT_DECLINED:
        if (nxt_slow_path(pq.own_whiteout != NULL)) {
            /* Previously deleted property. */
            prop = pq.own_whiteout;

            prop->type = NJS_PROPERTY;
            prop->enumerable = 1;
            prop->configurable = 1;
            prop->writable = 1;

            goto found;
        }

        break;

    case NJS_TRAP:
    case NXT_ERROR:
    default:

        return ret;
    }

    if (nxt_slow_path(!object->data.u.object->extensible)) {
        njs_type_error(vm, "Cannot add property \"%V\", "
                       "object is not extensible", &pq.lhq.key);
        return NXT_ERROR;
    }

    prop = njs_object_prop_alloc(vm, &pq.value, &njs_value_undefined, 1);
    if (nxt_slow_path(prop == NULL)) {
        return NXT_ERROR;
    }

    if (nxt_slow_path(shared != NULL)) {
        prop->enumerable = shared->enumerable;
        prop->configurable = shared->configurable;
    }

    pq.lhq.replace = 0;
    pq.lhq.value = prop;
    pq.lhq.pool = vm->mem_pool;

    ret = nxt_lvlhsh_insert(&object->data.u.object->hash, &pq.lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NXT_ERROR;
    }

found:

    prop->value = *value;

    return NXT_OK;
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

        prop->getter = njs_value_invalid;
        prop->setter = njs_value_invalid;

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
 * ES5.1, 8.12.9: [[DefineOwnProperty]]
 *   Limited support of special descriptors like length and array index
 *   (values can be set, but without property flags support).
 */
njs_ret_t
njs_object_prop_define(njs_vm_t *vm, njs_value_t *object,
    const njs_value_t *name, const njs_value_t *value)
{
    nxt_int_t             ret;
    njs_object_prop_t     *prop, *prev;
    njs_property_query_t  pq;

    njs_string_get(name, &pq.lhq.key);
    pq.lhq.key_hash = nxt_djb_hash(pq.lhq.key.start, pq.lhq.key.length);
    pq.lhq.proto = &njs_object_hash_proto;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 1);

    ret = njs_property_query(vm, &pq, object, name);

    if (ret != NXT_OK && ret != NXT_DECLINED) {
        return ret;
    }

    prop = njs_descriptor_prop(vm, name, value->data.u.object);
    if (nxt_slow_path(prop == NULL)) {
        return NXT_ERROR;
    }

    if (nxt_fast_path(ret == NXT_DECLINED)) {

        /* 6.2.5.6 CompletePropertypropriptor */

        if (njs_is_accessor_descriptor(prop)) {
            if (!njs_is_valid(&prop->getter)) {
                prop->getter = njs_value_undefined;
            }

            if (!njs_is_valid(&prop->setter)) {
                prop->setter = njs_value_undefined;
            }

        } else {
            if (prop->writable == NJS_ATTRIBUTE_UNSET) {
                prop->writable = 0;
            }

            if (!njs_is_valid(&prop->value)) {
                prop->value = njs_value_undefined;
            }
        }

        if (prop->enumerable == NJS_ATTRIBUTE_UNSET) {
            prop->enumerable = 0;
        }

        if (prop->configurable == NJS_ATTRIBUTE_UNSET) {
            prop->configurable = 0;
        }

        if (nxt_slow_path(pq.lhq.value != NULL)) {
            prev = pq.lhq.value;

            if (nxt_slow_path(prev->type == NJS_WHITEOUT)) {
                /* Previously deleted property.  */
                *prev = *prop;
            }

        } else {
            pq.lhq.value = prop;
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

    prev = pq.lhq.value;

    switch (prev->type) {
    case NJS_METHOD:
    case NJS_PROPERTY:
    case NJS_PROPERTY_HANDLER:
        break;

    case NJS_PROPERTY_REF:
        if (njs_is_valid(&prop->value)) {
            *prev->value.data.u.value = prop->value;
        } else {
            *prev->value.data.u.value = njs_value_undefined;
        }

        return NXT_OK;

    default:
        njs_internal_error(vm, "unexpected property type \"%s\" "
                           "while defining property",
                           njs_prop_type_string(prev->type));

        return NXT_ERROR;
    }

    /* 9.1.6.3 ValidateAndApplyPropertyDescriptor */

    if (!prev->configurable) {

        if (prop->configurable == NJS_ATTRIBUTE_TRUE) {
            goto exception;
        }

        if (prop->enumerable != NJS_ATTRIBUTE_UNSET
            && prev->enumerable != prop->enumerable)
        {
            goto exception;
        }
    }

    if (njs_is_generic_descriptor(prop)) {
        goto done;
    }

    if (njs_is_data_descriptor(prev) != njs_is_data_descriptor(prop)) {
        if (!prev->configurable) {
            goto exception;
        }

        /*
         * 6.b-c Preserve the existing values of the converted property's
         * [[Configurable]] and [[Enumerable]] attributes and set the rest of
         * the property's attributes to their default values.
         */

        if (njs_is_data_descriptor(prev)) {
            prev->getter = njs_value_undefined;
            prev->setter = njs_value_undefined;

            prev->value = njs_value_invalid;
            prev->writable = NJS_ATTRIBUTE_UNSET;

        } else {
            prev->value = njs_value_undefined;
            prev->writable = NJS_ATTRIBUTE_FALSE;

            prev->getter = njs_value_invalid;
            prev->setter = njs_value_invalid;
        }


    } else if (njs_is_data_descriptor(prev)
               && njs_is_data_descriptor(prop))
    {
        if (!prev->configurable && !prev->writable) {
            if (prop->writable == NJS_ATTRIBUTE_TRUE) {
                goto exception;
            }

            if (njs_is_valid(&prop->value)
                && prev->type != NJS_PROPERTY_HANDLER
                && !njs_values_strict_equal(&prop->value, &prev->value))
            {
                goto exception;
            }
        }

    } else {
        if (!prev->configurable) {
            if (njs_is_valid(&prop->getter)
                && !njs_values_strict_equal(&prop->getter, &prev->getter))
            {
                goto exception;
            }

            if (njs_is_valid(&prop->setter)
                && !njs_values_strict_equal(&prop->setter, &prev->setter))
            {
                goto exception;
            }
        }
    }

done:

    /*
     * 9. For each field of Desc that is present, set the corresponding
     * attribute of the property named P of object O to the value of the field.
     */

    if (njs_is_valid(&prop->getter)) {
        prev->getter = prop->getter;
    }

    if (njs_is_valid(&prop->setter)) {
        prev->setter = prop->setter;
    }

    if (njs_is_valid(&prop->value)) {
        if (prev->type == NJS_PROPERTY_HANDLER) {
            if (njs_is_data_descriptor(prev) && prev->writable) {
                ret = prev->value.data.u.prop_handler(vm, object,
                                                         &prop->value,
                                                         &vm->retval);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }
            }

        } else {
            prev->value = prop->value;
        }
    }

    if (prop->writable != NJS_ATTRIBUTE_UNSET) {
        prev->writable = prop->writable;
    }

    if (prop->enumerable != NJS_ATTRIBUTE_UNSET) {
        prev->enumerable = prop->enumerable;
    }

    if (prop->configurable != NJS_ATTRIBUTE_UNSET) {
        prev->configurable = prop->configurable;
    }

    return NXT_OK;

exception:

    njs_type_error(vm, "Cannot redefine property: \"%V\"", &pq.lhq.key);

    return NXT_ERROR;
}


static njs_object_prop_t *
njs_descriptor_prop(njs_vm_t *vm, const njs_value_t *name,
    const njs_object_t *desc)
{
    nxt_bool_t          data, accessor;
    njs_object_prop_t   *prop, *pr;
    const njs_value_t   *setter, *getter;
    nxt_lvlhsh_query_t  pq;

    data = 0;
    accessor = 0;

    prop = njs_object_prop_alloc(vm, name, &njs_value_invalid,
                                 NJS_ATTRIBUTE_UNSET);
    if (nxt_slow_path(prop == NULL)) {
        return NULL;
    }

    getter = &njs_value_invalid;
    pq.key = nxt_string_value("get");
    pq.key_hash = NJS_GET_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        if (!njs_is_undefined(&pr->value) && !njs_is_function(&pr->value)) {
            njs_type_error(vm, "Getter must be a function");
            return NULL;
        }

        accessor = 1;
        getter = &pr->value;
    }

    prop->getter = *getter;

    setter = &njs_value_invalid;
    pq.key = nxt_string_value("set");
    pq.key_hash = NJS_SET_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        if (!njs_is_undefined(&pr->value) && !njs_is_function(&pr->value)) {
            njs_type_error(vm, "Setter must be a function");
            return NULL;
        }

        accessor = 1;
        setter = &pr->value;
    }

    prop->setter = *setter;

    pq.key = nxt_string_value("value");
    pq.key_hash = NJS_VALUE_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        data = 1;
        prop->value = pr->value;
    }

    pq.key = nxt_string_value("writable");
    pq.key_hash = NJS_WRITABABLE_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        data = 1;
        prop->writable = pr->value.data.truth;
    }

    pq.key = nxt_string_value("enumerable");
    pq.key_hash = NJS_ENUMERABLE_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        prop->enumerable = pr->value.data.truth;
    }

    pq.key = nxt_string_value("configurable");
    pq.key_hash = NJS_CONFIGURABLE_HASH;

    pr = njs_object_property(vm, desc, &pq);
    if (pr != NULL) {
        prop->configurable = pr->value.data.truth;
    }

    if (accessor && data) {
        njs_type_error(vm, "Cannot both specify accessors "
                           "and a value or writable attribute");
        return NULL;
    }

    return prop;
}


static const njs_value_t  njs_object_value_string = njs_string("value");
static const njs_value_t  njs_object_get_string = njs_string("get");
static const njs_value_t  njs_object_set_string = njs_string("set");
static const njs_value_t  njs_object_writable_string =
                                                    njs_string("writable");
static const njs_value_t  njs_object_enumerable_string =
                                                    njs_string("enumerable");
static const njs_value_t  njs_object_configurable_string =
                                                    njs_string("configurable");


njs_ret_t
njs_object_prop_descriptor(njs_vm_t *vm, njs_value_t *dest,
    const njs_value_t *value, const njs_value_t *property)
{
    nxt_int_t             ret;
    njs_object_t          *desc;
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

    desc = njs_object_alloc(vm);
    if (nxt_slow_path(desc == NULL)) {
        return NXT_ERROR;
    }

    lhq.proto = &njs_object_hash_proto;
    lhq.replace = 0;
    lhq.pool = vm->mem_pool;

    if (njs_is_data_descriptor(prop)) {

        lhq.key = nxt_string_value("value");
        lhq.key_hash = NJS_VALUE_HASH;

        pr = njs_object_prop_alloc(vm, &njs_object_value_string, &prop->value,
                                   1);
        if (nxt_slow_path(pr == NULL)) {
            return NXT_ERROR;
        }

        lhq.value = pr;

        ret = nxt_lvlhsh_insert(&desc->hash, &lhq);
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

        ret = nxt_lvlhsh_insert(&desc->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NXT_ERROR;
        }

    } else {

        lhq.key = nxt_string_value("get");
        lhq.key_hash = NJS_GET_HASH;

        pr = njs_object_prop_alloc(vm, &njs_object_get_string, &prop->getter,
                                   1);
        if (nxt_slow_path(pr == NULL)) {
            return NXT_ERROR;
        }

        lhq.value = pr;

        ret = nxt_lvlhsh_insert(&desc->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NXT_ERROR;
        }

        lhq.key = nxt_string_value("set");
        lhq.key_hash = NJS_SET_HASH;

        pr = njs_object_prop_alloc(vm, &njs_object_set_string, &prop->setter,
                                   1);
        if (nxt_slow_path(pr == NULL)) {
            return NXT_ERROR;
        }

        lhq.value = pr;

        ret = nxt_lvlhsh_insert(&desc->hash, &lhq);
        if (nxt_slow_path(ret != NXT_OK)) {
            njs_internal_error(vm, "lvlhsh insert failed");
            return NXT_ERROR;
        }
    }

    lhq.key = nxt_string_value("enumerable");
    lhq.key_hash = NJS_ENUMERABLE_HASH;

    setval = (prop->enumerable == 1) ? &njs_value_true : &njs_value_false;

    pr = njs_object_prop_alloc(vm, &njs_object_enumerable_string, setval, 1);
    if (nxt_slow_path(pr == NULL)) {
        return NXT_ERROR;
    }

    lhq.value = pr;

    ret = nxt_lvlhsh_insert(&desc->hash, &lhq);
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

    ret = nxt_lvlhsh_insert(&desc->hash, &lhq);
    if (nxt_slow_path(ret != NXT_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NXT_ERROR;
    }

    dest->data.u.object = desc;
    dest->type = NJS_OBJECT;
    dest->data.truth = 1;

    return NXT_OK;
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


const char *
njs_prop_type_string(njs_object_prop_type_t type)
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
