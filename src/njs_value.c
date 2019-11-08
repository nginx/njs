
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t njs_object_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_object_t *object,
    const njs_value_t *key);
static njs_int_t njs_array_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_array_t *array, uint32_t index);
static njs_int_t njs_string_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_value_t *object, uint32_t index);
static njs_int_t njs_external_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_value_t *object);
static njs_int_t njs_external_property_set(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_external_property_delete(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);


const njs_value_t  njs_value_null =         njs_value(NJS_NULL, 0, 0.0);
const njs_value_t  njs_value_undefined =    njs_value(NJS_UNDEFINED, 0, NAN);
const njs_value_t  njs_value_false =        njs_value(NJS_BOOLEAN, 0, 0.0);
const njs_value_t  njs_value_true =         njs_value(NJS_BOOLEAN, 1, 1.0);
const njs_value_t  njs_value_zero =         njs_value(NJS_NUMBER, 0, 0.0);
const njs_value_t  njs_value_nan =          njs_value(NJS_NUMBER, 0, NAN);
const njs_value_t  njs_value_invalid =      njs_value(NJS_INVALID, 0, 0.0);

const njs_value_t  njs_string_empty =       njs_string("");
const njs_value_t  njs_string_comma =       njs_string(",");
const njs_value_t  njs_string_null =        njs_string("null");
const njs_value_t  njs_string_undefined =   njs_string("undefined");
const njs_value_t  njs_string_boolean =     njs_string("boolean");
const njs_value_t  njs_string_false =       njs_string("false");
const njs_value_t  njs_string_true =        njs_string("true");
const njs_value_t  njs_string_number =      njs_string("number");
const njs_value_t  njs_string_minus_zero =  njs_string("-0");
const njs_value_t  njs_string_minus_infinity =
                                            njs_string("-Infinity");
const njs_value_t  njs_string_plus_infinity =
                                            njs_string("Infinity");
const njs_value_t  njs_string_nan =         njs_string("NaN");
const njs_value_t  njs_string_string =      njs_string("string");
const njs_value_t  njs_string_name =        njs_string("name");
const njs_value_t  njs_string_data =        njs_string("data");
const njs_value_t  njs_string_external =    njs_string("external");
const njs_value_t  njs_string_invalid =     njs_string("invalid");
const njs_value_t  njs_string_object =      njs_string("object");
const njs_value_t  njs_string_function =    njs_string("function");
const njs_value_t  njs_string_memory_error = njs_string("MemoryError");


void
njs_value_retain(njs_value_t *value)
{
    njs_string_t  *string;

    if (njs_is_string(value)) {

        if (value->long_string.external != 0xff) {
            string = value->long_string.data;

            njs_thread_log_debug("retain:%uxD \"%*s\"", string->retain,
                                 value->long_string.size, string->start);

            if (string->retain != 0xffff) {
                string->retain++;
            }
        }
    }
}


void
njs_value_release(njs_vm_t *vm, njs_value_t *value)
{
    njs_string_t  *string;

    if (njs_is_string(value)) {

        if (value->long_string.external != 0xff) {
            string = value->long_string.data;

            njs_thread_log_debug("release:%uxD \"%*s\"", string->retain,
                                 value->long_string.size, string->start);

            if (string->retain != 0xffff) {
                string->retain--;

#if 0
                if (string->retain == 0) {
                    if ((u_char *) string + sizeof(njs_string_t)
                        != string->start)
                    {
                        njs_memcache_pool_free(vm->mem_pool,
                                               string->start);
                    }

                    njs_memcache_pool_free(vm->mem_pool, string);
                }
#endif
            }
        }
    }
}


/*
 * A hint value is 0 for numbers and 1 for strings.  The value chooses
 * method calls order specified by ECMAScript 5.1: "valueOf", "toString"
 * for numbers and "toString", "valueOf" for strings.
 */

njs_int_t
njs_value_to_primitive(njs_vm_t *vm, njs_value_t *dst, njs_value_t *value,
    njs_uint_t hint)
{
    njs_int_t           ret;
    njs_uint_t          tries;
    njs_value_t         method, retval;
    njs_lvlhsh_query_t  lhq;

    static const uint32_t  hashes[] = {
        NJS_VALUE_OF_HASH,
        NJS_TO_STRING_HASH,
    };

    static const njs_str_t  names[] = {
        njs_str("valueOf"),
        njs_str("toString"),
    };


    if (njs_is_primitive(value)) {
        /* GC */
        *dst = *value;
        return NJS_OK;
    }

    tries = 0;
    lhq.proto = &njs_object_hash_proto;

    for ( ;; ) {
        ret = NJS_ERROR;

        if (njs_is_object(value) && tries < 2) {
            hint ^= tries++;

            lhq.key_hash = hashes[hint];
            lhq.key = names[hint];

            ret = njs_object_property(vm, value, &lhq, &method);

            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            if (njs_is_function(&method)) {
                ret = njs_function_apply(vm, njs_function(&method), value, 1,
                                         &retval);

                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }

                if (njs_is_primitive(&retval)) {
                    break;
                }
            }

            /* Try the second method. */
            continue;
         }

        njs_type_error(vm, "Cannot convert object to primitive value");

        return ret;
    }

    *dst = retval;

    return NJS_OK;
}


njs_array_t *
njs_value_enumerate(njs_vm_t *vm, const njs_value_t *value,
    njs_object_enum_t kind, njs_bool_t all)
{
    void                *obj;
    njs_int_t           ret;
    njs_value_t         keys;
    njs_object_value_t  obj_val;
    const njs_extern_t  *ext_proto;

    if (njs_is_object(value)) {
        return njs_object_enumerate(vm, njs_object(value), kind, all);
    }

    if (value->type != NJS_STRING) {
        if (kind == NJS_ENUM_KEYS && njs_is_external(value)) {
            ext_proto = value->external.proto;

            if (ext_proto->keys != NULL) {
                obj = njs_extern_object(vm, value);

                ret = ext_proto->keys(vm, obj, &keys);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NULL;
                }

                return njs_array(&keys);
            }

            return njs_extern_keys_array(vm, ext_proto);
        }

        return njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
    }

    obj_val.object = vm->string_object;
    obj_val.value = *value;

    return njs_object_enumerate(vm, (njs_object_t *) &obj_val, kind, all);
}


njs_array_t *
njs_value_own_enumerate(njs_vm_t *vm, const njs_value_t *value,
    njs_object_enum_t kind, njs_bool_t all)
{
    void                *obj;
    njs_int_t           ret;
    njs_value_t         keys;
    njs_object_value_t  obj_val;
    const njs_extern_t  *ext_proto;

    if (njs_is_object(value)) {
        return njs_object_own_enumerate(vm, njs_object(value), kind, all);
    }

    if (value->type != NJS_STRING) {
        if (kind == NJS_ENUM_KEYS && njs_is_external(value)) {
            ext_proto = value->external.proto;

            if (ext_proto->keys != NULL) {
                obj = njs_extern_object(vm, value);

                ret = ext_proto->keys(vm, obj, &keys);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NULL;
                }

                return njs_array(&keys);
            }

            return njs_extern_keys_array(vm, ext_proto);
        }

        return njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
    }

    obj_val.object = vm->string_object;
    obj_val.value = *value;

    return njs_object_own_enumerate(vm, (njs_object_t *) &obj_val, kind, all);
}


njs_int_t
njs_value_length(njs_vm_t *vm, njs_value_t *value, uint32_t *length)
{
    njs_string_prop_t  string_prop;

    if (njs_is_string(value)) {
        *length = (uint32_t) njs_string_prop(&string_prop, value);

    } else if (njs_is_primitive(value)) {
        *length = 0;

    } else if (njs_is_array(value)) {
        *length = njs_array_len(value);

    } else {
        return njs_object_length(vm, value, length);
    }

    return NJS_OK;
}


const char *
njs_type_string(njs_value_type_t type)
{
    switch (type) {
    case NJS_NULL:
        return "null";

    case NJS_UNDEFINED:
        return "undefined";

    case NJS_BOOLEAN:
        return "boolean";

    case NJS_NUMBER:
        return "number";

    case NJS_STRING:
        return "string";

    case NJS_EXTERNAL:
        return "external";

    case NJS_INVALID:
        return "invalid";

    case NJS_OBJECT:
        return "object";

    case NJS_ARRAY:
        return "array";

    case NJS_OBJECT_BOOLEAN:
        return "object boolean";

    case NJS_OBJECT_NUMBER:
        return "object number";

    case NJS_OBJECT_STRING:
        return "object string";

    case NJS_FUNCTION:
        return "function";

    case NJS_REGEXP:
        return "regexp";

    case NJS_DATE:
        return "date";

    default:
        return NULL;
    }
}


void
njs_value_undefined_set(njs_value_t *value)
{
    njs_set_undefined(value);
}


void
njs_value_boolean_set(njs_value_t *value, int yn)
{
    njs_set_boolean(value, yn);
}


void
njs_value_number_set(njs_value_t *value, double num)
{
    njs_set_number(value, num);
}


void
njs_value_data_set(njs_value_t *value, void *data)
{
    njs_set_data(value, data);
}


uint8_t
njs_value_bool(const njs_value_t *value)
{
    return njs_bool(value);
}


double
njs_value_number(const njs_value_t *value)
{
    return njs_number(value);
}


void *
njs_value_data(const njs_value_t *value)
{
    return njs_data(value);
}


njs_function_t *
njs_value_function(const njs_value_t *value)
{
    return njs_function(value);
}


njs_int_t
njs_value_is_null(const njs_value_t *value)
{
    return njs_is_null(value);
}


njs_int_t
njs_value_is_undefined(const njs_value_t *value)
{
    return njs_is_undefined(value);
}


njs_int_t
njs_value_is_null_or_undefined(const njs_value_t *value)
{
    return njs_is_null_or_undefined(value);
}


njs_int_t
njs_value_is_boolean(const njs_value_t *value)
{
    return njs_is_boolean(value);
}


njs_int_t
njs_value_is_number(const njs_value_t *value)
{
    return njs_is_number(value);
}


njs_int_t
njs_value_is_valid_number(const njs_value_t *value)
{
    return njs_is_number(value)
           && !isnan(njs_number(value))
           && !isinf(njs_number(value));
}


njs_int_t
njs_value_is_string(const njs_value_t *value)
{
    return njs_is_string(value);
}


njs_int_t
njs_value_is_object(const njs_value_t *value)
{
    return njs_is_object(value);
}


njs_int_t
njs_value_is_function(const njs_value_t *value)
{
    return njs_is_function(value);
}


/*
 * ES5.1, 8.12.1: [[GetOwnProperty]], [[GetProperty]].
 * The njs_property_query() returns values
 *   NJS_OK               property has been found in object,
 *     retval of type njs_object_prop_t * is in pq->lhq.value.
 *     in NJS_PROPERTY_QUERY_GET
 *       prop->type is NJS_PROPERTY or NJS_PROPERTY_HANDLER.
 *     in NJS_PROPERTY_QUERY_SET, NJS_PROPERTY_QUERY_DELETE
 *       prop->type is NJS_PROPERTY, NJS_PROPERTY_REF or
 *       NJS_PROPERTY_HANDLER.
 *   NJS_DECLINED         property was not found in object,
 *     if pq->lhq.value != NULL it contains retval of type
 *     njs_object_prop_t * where prop->type is NJS_WHITEOUT
 *   NJS_ERROR            exception has been thrown.
 *
 *   TODO:
 *     Object.defineProperty([1,2], '1', {configurable:false})
 */

njs_int_t
njs_property_query(njs_vm_t *vm, njs_property_query_t *pq, njs_value_t *value,
    njs_value_t *key)
{
    uint32_t        index;
    njs_int_t       ret;
    njs_object_t    *obj;
    njs_value_t     prop;
    njs_function_t  *function;

    if (njs_slow_path(!njs_is_primitive(key))) {
        ret = njs_value_to_string(vm, &prop, key);
        if (ret != NJS_OK) {
            return ret;
        }

        key = &prop;
    }

    switch (value->type) {

    case NJS_BOOLEAN:
    case NJS_NUMBER:
        index = njs_primitive_prototype_index(value->type);
        obj = &vm->prototypes[index].object;
        break;

    case NJS_STRING:
        if (njs_fast_path(!njs_is_null_or_undefined_or_boolean(key))) {
            index = njs_value_to_index(key);

            if (njs_fast_path(index < NJS_STRING_MAX_LENGTH)) {
                return njs_string_property_query(vm, pq, value, index);
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
    case NJS_OBJECT_VALUE:
        obj = njs_object(value);
        break;

    case NJS_FUNCTION:
        function = njs_function_value_copy(vm, value);
        if (njs_slow_path(function == NULL)) {
            return NJS_ERROR;
        }

        obj = &function->object;
        break;

    case NJS_EXTERNAL:
        obj = NULL;
        break;

    case NJS_UNDEFINED:
    case NJS_NULL:
    default:
        ret = njs_primitive_value_to_string(vm, &pq->key, key);

        if (njs_fast_path(ret == NJS_OK)) {
            njs_string_get(&pq->key, &pq->lhq.key);
            njs_type_error(vm, "cannot get property \"%V\" of undefined",
                           &pq->lhq.key);
            return NJS_ERROR;
        }

        njs_type_error(vm, "cannot get property \"unknown\" of undefined");

        return NJS_ERROR;
    }

    ret = njs_primitive_value_to_string(vm, &pq->key, key);

    if (njs_fast_path(ret == NJS_OK)) {

        njs_string_get(&pq->key, &pq->lhq.key);
        pq->lhq.key_hash = njs_djb_hash(pq->lhq.key.start, pq->lhq.key.length);

        if (obj == NULL) {
            pq->own = 1;
            return njs_external_property_query(vm, pq, value);
        }

        return njs_object_property_query(vm, pq, obj, key);
    }

    return ret;
}


static njs_int_t
njs_object_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *object, const njs_value_t *key)
{
    uint32_t            index;
    njs_int_t           ret;
    njs_bool_t          own;
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

        if (!njs_is_null_or_undefined_or_boolean(key)) {
            switch (proto->type) {
            case NJS_ARRAY:
                index = njs_value_to_index(key);
                if (njs_fast_path(index < NJS_ARRAY_MAX_INDEX)) {
                    array = (njs_array_t *) proto;
                    return njs_array_property_query(vm, pq, array, index);
                }

                break;

            case NJS_OBJECT_STRING:
                index = njs_value_to_index(key);
                if (njs_fast_path(index < NJS_STRING_MAX_LENGTH)) {
                    ov = (njs_object_value_t *) proto;
                    ret = njs_string_property_query(vm, pq, &ov->value, index);

                    if (njs_fast_path(ret != NJS_DECLINED)) {
                        return ret;
                    }
                }

            default:
                break;
            }
        }

        ret = njs_lvlhsh_find(&proto->hash, &pq->lhq);

        if (ret == NJS_OK) {
            prop = pq->lhq.value;

            if (prop->type != NJS_WHITEOUT) {
                return ret;
            }

            if (pq->own) {
                pq->own_whiteout = prop;
            }

        } else {
            ret = njs_lvlhsh_find(&proto->shared_hash, &pq->lhq);

            if (ret == NJS_OK) {
                prop = pq->lhq.value;

                if (!prop->configurable
                    && prop->type == NJS_PROPERTY_HANDLER)
                {
                    /* Postpone making a mutable NJS_PROPERTY_HANDLER copy. */
                    pq->shared = 1;
                    return ret;
                }

                return njs_prop_private_copy(vm, pq);
            }
        }

        if (own) {
            return NJS_DECLINED;
        }

        pq->own = 0;
        proto = proto->__proto__;

    } while (proto != NULL);

    return NJS_DECLINED;
}


static njs_int_t
njs_array_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_array_t *array, uint32_t index)
{
    uint32_t           size;
    njs_int_t          ret;
    njs_value_t        *value;
    njs_object_prop_t  *prop;

    if (index >= array->length) {
        if (pq->query != NJS_PROPERTY_QUERY_SET) {
            return NJS_DECLINED;
        }

        size = index - array->length;

        ret = njs_array_expand(vm, array, 0, size + 1);
        if (njs_slow_path(ret != NJS_OK)) {
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
            return NJS_DECLINED;
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

    return NJS_OK;
}


static njs_int_t
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
            /* pq->lhq.key is used by NJS_VMCODE_PROPERTY_SET for TypeError */
            njs_uint32_to_string(&pq->key, index);
            njs_string_get(&pq->key, &pq->lhq.key);
        }

        return NJS_OK;
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_external_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_value_t *object)
{
    void                *obj;
    njs_int_t           ret;
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
    ret = njs_lvlhsh_find(&ext_proto->hash, &pq->lhq);

    if (ret == NJS_OK) {
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
            if (njs_slow_path(ret != NJS_OK)) {
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

        return NJS_OK;
    }

done:

    if (ext_proto->type == NJS_EXTERN_METHOD) {
        njs_set_function(&prop->value, ext_proto->function);
    }

    pq->lhq.value = prop;

    return ret;
}


static njs_int_t
njs_external_property_set(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    void                  *obj;
    njs_int_t             ret;
    njs_str_t             s;
    njs_property_query_t  *pq;

    pq = (njs_property_query_t *) vm->stash;

    if (!njs_is_null_or_undefined(setval)) {
        ret = njs_vm_value_to_string(vm, &s, setval);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {
        s = njs_str_value("");
    }

    *retval = *setval;

    obj = njs_extern_index(vm, pq->ext_index);

    return pq->ext_proto->set(vm, obj, pq->ext_data, &s);
}


static njs_int_t
njs_external_property_delete(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *unused, njs_value_t *unused2)
{
    void                  *obj;
    njs_property_query_t  *pq;

    pq = (njs_property_query_t *) vm->stash;

    obj = njs_extern_index(vm, pq->ext_index);

    return pq->ext_proto->find(vm, obj, pq->ext_data, 1);
}


njs_int_t
njs_value_property(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *retval)
{
    njs_int_t             ret;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0);

    ret = njs_property_query(vm, &pq, value, key);

    switch (ret) {

    case NJS_OK:
        prop = pq.lhq.value;

        switch (prop->type) {

        case NJS_PROPERTY:
            if (njs_is_data_descriptor(prop)) {
                *retval = prop->value;
                break;
            }

            if (njs_is_undefined(&prop->getter)) {
                njs_set_undefined(retval);
                break;
            }

            return njs_function_apply(vm, njs_function(&prop->getter), value,
                                      1, retval);

        case NJS_PROPERTY_HANDLER:
            pq.scratch = *prop;
            prop = &pq.scratch;
            ret = prop->value.data.u.prop_handler(vm, prop, value, NULL,
                                                  &prop->value);

            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            *retval = prop->value;

            break;

        default:
            njs_internal_error(vm, "unexpected property type \"%s\" "
                               "while getting",
                               njs_prop_type_string(prop->type));

            return NJS_ERROR;
        }

        break;

    case NJS_DECLINED:
        njs_set_undefined(retval);

        return NJS_DECLINED;

    case NJS_ERROR:
    default:

        return ret;
    }

    return NJS_OK;
}


njs_int_t
njs_value_property_set(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *setval)
{
    njs_int_t             ret;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    if (njs_is_primitive(value)) {
        njs_type_error(vm, "property set on primitive %s type",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 0);

    ret = njs_property_query(vm, &pq, value, key);

    switch (ret) {

    case NJS_OK:
        prop = pq.lhq.value;

        if (njs_is_data_descriptor(prop)) {
            if (!prop->writable) {
                njs_type_error(vm,
                             "Cannot assign to read-only property \"%V\" of %s",
                               &pq.lhq.key, njs_type_string(value->type));
                return NJS_ERROR;
            }

        } else {
            if (njs_is_function(&prop->setter)) {
                return njs_function_call(vm, njs_function(&prop->setter),
                                         value, setval, 1, &vm->retval);
            }

            njs_type_error(vm,
                     "Cannot set property \"%V\" of %s which has only a getter",
                           &pq.lhq.key, njs_type_string(value->type));
            return NJS_ERROR;
        }

        if (prop->type == NJS_PROPERTY_HANDLER) {
            ret = prop->value.data.u.prop_handler(vm, prop, value, setval,
                                                  &vm->retval);
            if (njs_slow_path(ret != NJS_DECLINED)) {
                return ret;
            }
        }

        if (pq.own) {
            switch (prop->type) {
            case NJS_PROPERTY:
                goto found;

            case NJS_PROPERTY_REF:
                *prop->value.data.u.value = *setval;
                return NJS_OK;

            default:
                njs_internal_error(vm, "unexpected property type \"%s\" "
                                   "while setting",
                                   njs_prop_type_string(prop->type));

                return NJS_ERROR;
            }

            break;
        }

        /* Fall through. */

    case NJS_DECLINED:
        if (njs_slow_path(pq.own_whiteout != NULL)) {
            /* Previously deleted property. */
            prop = pq.own_whiteout;

            prop->type = NJS_PROPERTY;
            prop->enumerable = 1;
            prop->configurable = 1;
            prop->writable = 1;

            goto found;
        }

        break;

    case NJS_ERROR:
    default:

        return ret;
    }

    if (njs_slow_path(!njs_object(value)->extensible)) {
        njs_type_error(vm, "Cannot add property \"%V\", "
                       "object is not extensible", &pq.lhq.key);
        return NJS_ERROR;
    }

    prop = njs_object_prop_alloc(vm, &pq.key, &njs_value_undefined, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    pq.lhq.replace = 0;
    pq.lhq.value = prop;
    pq.lhq.pool = vm->mem_pool;

    ret = njs_lvlhsh_insert(njs_object_hash(value), &pq.lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "lvlhsh insert failed");
        return NJS_ERROR;
    }

found:

    prop->value = *setval;

    return NJS_OK;
}


njs_int_t
njs_value_property_delete(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *removed)
{
    njs_int_t             ret;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_DELETE, 1);

    ret = njs_property_query(vm, &pq, value, key);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    prop = pq.lhq.value;

    if (njs_slow_path(!prop->configurable)) {
        njs_type_error(vm, "Cannot delete property \"%V\" of %s",
                       &pq.lhq.key, njs_type_string(value->type));
        return NJS_ERROR;
    }

    switch (prop->type) {
    case NJS_PROPERTY_HANDLER:
        if (njs_is_external(value)) {
            ret = prop->value.data.u.prop_handler(vm, prop, value, NULL, NULL);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            return NJS_OK;
        }

        /* Fall through. */

    case NJS_PROPERTY:
        break;

    case NJS_PROPERTY_REF:
        if (removed != NULL) {
            *removed = *prop->value.data.u.value;
        }

        njs_set_invalid(prop->value.data.u.value);
        return NJS_OK;

    default:
        njs_internal_error(vm, "unexpected property type \"%s\" "
                           "while deleting", njs_prop_type_string(prop->type));
        return NJS_ERROR;
    }

    /* GC: release value. */
    if (removed != NULL) {
        *removed = prop->value;
    }

    prop->type = NJS_WHITEOUT;
    njs_set_invalid(&prop->value);

    return NJS_OK;
}


njs_int_t
njs_value_to_object(njs_vm_t *vm, njs_value_t *value)
{
    njs_object_t  *object;

    if (njs_slow_path(njs_is_null_or_undefined(value))) {
        njs_type_error(vm, "cannot convert null or undefined to object");
        return NJS_ERROR;
    }

    if (njs_fast_path(njs_is_object(value))) {
        return NJS_OK;
    }

    if (njs_is_primitive(value)) {
        object = njs_object_value_alloc(vm, value, value->type);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        njs_set_type_object(value, object, njs_object_value_type(value->type));

        return NJS_OK;
    }

    njs_type_error(vm, "cannot convert %s to object",
                   njs_type_string(value->type));

    return NJS_ERROR;
}
