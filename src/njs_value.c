
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
static njs_int_t njs_typed_array_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_typed_array_t *array, uint32_t index);
static njs_int_t njs_string_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_value_t *object, uint32_t index);
static njs_int_t njs_external_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_value_t *value);


const njs_value_t  njs_value_null =         njs_value(NJS_NULL, 0, 0.0);
const njs_value_t  njs_value_undefined =    njs_value(NJS_UNDEFINED, 0, NAN);
const njs_value_t  njs_value_false =        njs_value(NJS_BOOLEAN, 0, 0.0);
const njs_value_t  njs_value_true =         njs_value(NJS_BOOLEAN, 1, 1.0);
const njs_value_t  njs_value_zero =         njs_value(NJS_NUMBER, 0, 0.0);
const njs_value_t  njs_value_nan =          njs_value(NJS_NUMBER, 0, NAN);
const njs_value_t  njs_value_invalid =      njs_value(NJS_INVALID, 0, 0.0);

const njs_value_t  njs_string_empty =       njs_string("");
const njs_value_t  njs_string_empty_regexp =
                                            njs_string("(?:)");
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
const njs_value_t  njs_string_symbol =      njs_string("symbol");
const njs_value_t  njs_string_string =      njs_string("string");
const njs_value_t  njs_string_name =        njs_string("name");
const njs_value_t  njs_string_data =        njs_string("data");
const njs_value_t  njs_string_type =        njs_string("type");
const njs_value_t  njs_string_external =    njs_string("external");
const njs_value_t  njs_string_invalid =     njs_string("invalid");
const njs_value_t  njs_string_object =      njs_string("object");
const njs_value_t  njs_string_function =    njs_string("function");
const njs_value_t  njs_string_anonymous =   njs_string("anonymous");
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
njs_value_enumerate(njs_vm_t *vm, njs_value_t *value,
    njs_object_enum_t kind, njs_object_enum_type_t type, njs_bool_t all)
{
    njs_int_t           ret;
    njs_value_t         keys;
    njs_object_value_t  obj_val;
    njs_exotic_slots_t  *slots;

    if (njs_is_object(value)) {
        if (kind == NJS_ENUM_KEYS && (type & NJS_ENUM_STRING)) {
            slots = njs_object_slots(value);
            if (slots != NULL && slots->keys != NULL) {
                ret = slots->keys(vm, value, &keys);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NULL;
                }

                return njs_array(&keys);
            }
        }

        return njs_object_enumerate(vm, njs_object(value), kind, type, all);
    }

    if (value->type != NJS_STRING) {
        return njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
    }

    obj_val.object = vm->string_object;
    obj_val.value = *value;

    return njs_object_enumerate(vm, (njs_object_t *) &obj_val, kind, type, all);
}


njs_array_t *
njs_value_own_enumerate(njs_vm_t *vm, njs_value_t *value,
    njs_object_enum_t kind, njs_object_enum_type_t type, njs_bool_t all)
{
    njs_int_t           ret;
    njs_value_t         keys;
    njs_object_value_t  obj_val;
    njs_exotic_slots_t  *slots;

    if (njs_is_object(value)) {
        if (kind == NJS_ENUM_KEYS && (type & NJS_ENUM_STRING)) {
            slots = njs_object_slots(value);
            if (slots != NULL && slots->keys != NULL) {
                ret = slots->keys(vm, value, &keys);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NULL;
                }

                return njs_array(&keys);
            }
        }

        return njs_object_own_enumerate(vm, njs_object(value), kind, type, all);
    }

    if (value->type != NJS_STRING) {
        return njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
    }

    obj_val.object = vm->string_object;
    obj_val.value = *value;

    return njs_object_own_enumerate(vm, (njs_object_t *) &obj_val, kind,
                                    type, all);
}


njs_int_t
njs_value_of(njs_vm_t *vm, njs_value_t *value, njs_value_t *retval)
{

    njs_int_t  ret;

    static const njs_value_t  value_of = njs_string("valueOf");

    if (njs_slow_path(!njs_is_object(value))) {
        return NJS_DECLINED;
    }

    ret = njs_value_property(vm, value, njs_value_arg(&value_of),
                             retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!njs_is_function(retval)) {
        njs_type_error(vm, "object.valueOf is not a function");
        return NJS_ERROR;
    }

    return njs_function_apply(vm, njs_function(retval), value, 1, retval);
}


njs_int_t
njs_value_length(njs_vm_t *vm, njs_value_t *value, int64_t *length)
{
    njs_string_prop_t  string_prop;

    if (njs_is_string(value)) {
        *length = njs_string_prop(&string_prop, value);

    } else if (njs_is_primitive(value)) {
        *length = 0;

    } else if (njs_is_fast_array(value)) {
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

    case NJS_SYMBOL:
        return "symbol";

    case NJS_STRING:
        return "string";

    case NJS_INVALID:
        return "invalid";

    case NJS_OBJECT:
    case NJS_OBJECT_VALUE:
        return "object";

    case NJS_ARRAY:
        return "array";

    case NJS_ARRAY_BUFFER:
        return "array buffer";

    case NJS_TYPED_ARRAY:
        return "typed array";

    case NJS_FUNCTION:
        return "function";

    case NJS_REGEXP:
        return "regexp";

    case NJS_DATE:
        return "date";

    case NJS_PROMISE:
        return "promise";

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
njs_value_null_set(njs_value_t *value)
{
    njs_set_null(value);
}


void
njs_value_invalid_set(njs_value_t *value)
{
    njs_set_invalid(value);
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
njs_value_is_valid(const njs_value_t *value)
{
    return njs_is_valid(value);
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
njs_value_is_array(const njs_value_t *value)
{
    return njs_is_array(value);
}


njs_int_t
njs_value_is_function(const njs_value_t *value)
{
    return njs_is_function(value);
}


njs_int_t
njs_value_is_buffer(const njs_value_t *value)
{
    return njs_is_typed_array(value);
}


/*
 * ES5.1, 8.12.1: [[GetOwnProperty]], [[GetProperty]].
 * The njs_property_query() returns values
 *   NJS_OK               property has been found in object,
 *     retval of type njs_object_prop_t * is in pq->lhq.value.
 *     in NJS_PROPERTY_QUERY_GET
 *       prop->type is NJS_PROPERTY or NJS_PROPERTY_HANDLER.
 *     in NJS_PROPERTY_QUERY_SET, NJS_PROPERTY_QUERY_DELETE
 *       prop->type is NJS_PROPERTY, NJS_PROPERTY_REF, NJS_PROPERTY_PLACE_REF,
 *       NJS_PROPERTY_TYPED_ARRAY_REF or
 *       NJS_PROPERTY_HANDLER.
 *   NJS_DECLINED         property was not found in object,
 *     if pq->lhq.value != NULL it contains retval of type
 *     njs_object_prop_t * where prop->type is NJS_WHITEOUT
 *   NJS_ERROR            exception has been thrown.
 */

njs_int_t
njs_property_query(njs_vm_t *vm, njs_property_query_t *pq, njs_value_t *value,
    njs_value_t *key)
{
    double          num;
    uint32_t        index;
    njs_int_t       ret;
    njs_object_t    *obj;
    njs_function_t  *function;

    njs_assert(njs_is_index_or_key(key));

    switch (value->type) {
    case NJS_BOOLEAN:
    case NJS_NUMBER:
    case NJS_SYMBOL:
        index = njs_primitive_prototype_index(value->type);
        obj = &vm->prototypes[index].object;
        break;

    case NJS_STRING:
        if (njs_fast_path(!njs_is_null_or_undefined_or_boolean(key))) {
            num = njs_key_to_index(key);
            if (njs_fast_path(njs_key_is_integer_index(num, key))) {
                return njs_string_property_query(vm, pq, value, num);
            }
        }

        obj = &vm->string_object;
        break;

    case NJS_OBJECT:
    case NJS_ARRAY:
    case NJS_ARRAY_BUFFER:
    case NJS_DATA_VIEW:
    case NJS_TYPED_ARRAY:
    case NJS_REGEXP:
    case NJS_DATE:
    case NJS_PROMISE:
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

    case NJS_UNDEFINED:
    case NJS_NULL:
    default:
        ret = njs_primitive_value_to_string(vm, &pq->key, key);

        if (njs_fast_path(ret == NJS_OK)) {
            njs_string_get(&pq->key, &pq->lhq.key);
            njs_type_error(vm, "cannot get property \"%V\" of %s",
                           &pq->lhq.key, njs_is_null(value) ? "null"
                                                            : "undefined");
            return NJS_ERROR;
        }

        njs_type_error(vm, "cannot get property \"unknown\" of %s",
                       njs_is_null(value) ? "null" : "undefined");

        return NJS_ERROR;
    }

    ret = njs_primitive_value_to_key(vm, &pq->key, key);

    if (njs_fast_path(ret == NJS_OK)) {

        if (njs_is_symbol(key)) {
            pq->lhq.key_hash = njs_symbol_key(key);
            pq->lhq.key.start = NULL;

        } else {
            njs_string_get(&pq->key, &pq->lhq.key);

            if (pq->lhq.key_hash == 0) {
                pq->lhq.key_hash = njs_djb_hash(pq->lhq.key.start,
                                                pq->lhq.key.length);
            }
        }

        ret = njs_object_property_query(vm, pq, obj, key);

        if (njs_slow_path(ret == NJS_DECLINED && obj->slots != NULL)) {
            return njs_external_property_query(vm, pq, value);
        }
    }

    return ret;
}


static njs_int_t
njs_object_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *object, const njs_value_t *key)
{
    double              num;
    njs_int_t           ret;
    njs_bool_t          own;
    njs_array_t         *array;
    njs_object_t        *proto;
    njs_object_prop_t   *prop;
    njs_typed_array_t   *tarray;
    njs_object_value_t  *ov;

    pq->lhq.proto = &njs_object_hash_proto;

    own = pq->own;
    pq->own = 1;

    proto = object;

    do {
        switch (proto->type) {
        case NJS_ARRAY:
            array = (njs_array_t *) proto;
            num = njs_key_to_index(key);

            if (njs_fast_path(njs_key_is_integer_index(num, key))) {
                ret = njs_array_property_query(vm, pq, array, num);
                if (njs_fast_path(ret != NJS_DECLINED)) {
                    return (ret == NJS_DONE) ? NJS_DECLINED : ret;
                }
            }

            break;

        case NJS_TYPED_ARRAY:
            num = njs_key_to_index(key);
            if (njs_fast_path(njs_key_is_integer_index(num, key))) {
                tarray = (njs_typed_array_t *) proto;
                return njs_typed_array_property_query(vm, pq, tarray, num);
            }

            if (!isnan(num)) {
                return NJS_DECLINED;
            }

            break;

        case NJS_OBJECT_VALUE:
            ov = (njs_object_value_t *) proto;
            if (!njs_is_string(&ov->value)) {
                break;
            }

            num = njs_key_to_index(key);
            if (njs_fast_path(njs_key_is_integer_index(num, key))) {
                ov = (njs_object_value_t *) proto;
                ret = njs_string_property_query(vm, pq, &ov->value, num);
                if (njs_fast_path(ret != NJS_DECLINED)) {
                    return ret;
                }
            }

            break;

        default:
            break;
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
                return njs_prop_private_copy(vm, pq, proto);
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
    int64_t            length;
    uint64_t           size;
    njs_int_t          ret;
    njs_bool_t         resized;
    njs_value_t        *setval, value;
    njs_object_prop_t  *prop;

    resized = 0;

    if (pq->query == NJS_PROPERTY_QUERY_SET) {
        if (!array->object.extensible) {
            return NJS_DECLINED;
        }

        if (njs_fast_path(array->object.fast_array)) {
            if (njs_fast_path(index < NJS_ARRAY_LARGE_OBJECT_LENGTH)) {
                if (index >= array->length) {
                    size = index - array->length + 1;

                    ret = njs_array_expand(vm, array, 0, size);
                    if (njs_slow_path(ret != NJS_OK)) {
                        return ret;
                    }

                    setval = &array->start[array->length];

                    while (size != 0) {
                        njs_set_invalid(setval);
                        setval++;
                        size--;
                    }

                    array->length = index + 1;
                    resized = 1;
                }

                goto prop;
            }

            ret = njs_array_convert_to_slow_array(vm, array);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        njs_set_array(&value, array);

        ret = njs_object_length(vm, &value, &length);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if ((index + 1) > length) {
            ret = njs_array_length_redefine(vm, &value, index + 1, 1);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        ret = njs_lvlhsh_find(&array->object.hash, &pq->lhq);
        if (ret == NJS_OK) {
            prop = pq->lhq.value;

            if (prop->type != NJS_WHITEOUT) {
                return NJS_OK;
            }

            if (pq->own) {
                pq->own_whiteout = prop;
            }

            return NJS_DECLINED;
        }

        return NJS_DONE;
    }

    if (njs_slow_path(!array->object.fast_array)) {
        return NJS_DECLINED;
    }

    if (index >= array->length) {
        return NJS_DECLINED;
    }

prop:

    prop = &pq->scratch;

    if (pq->query == NJS_PROPERTY_QUERY_GET) {
        if (!njs_is_valid(&array->start[index])) {
            return NJS_DECLINED;
        }

        njs_value_assign(njs_prop_value(prop), &array->start[index]);
        prop->type = NJS_PROPERTY;

    } else {
        njs_prop_ref(prop) = &array->start[index];
        prop->type = resized ? NJS_PROPERTY_PLACE_REF : NJS_PROPERTY_REF;
    }

    njs_set_number(&prop->name, index);

    prop->writable = 1;
    prop->enumerable = 1;
    prop->configurable = 1;

    pq->lhq.value = prop;

    return NJS_OK;
}


static njs_int_t
njs_typed_array_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_typed_array_t *array, uint32_t index)
{
    njs_object_prop_t  *prop;

    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    if (index >= njs_typed_array_length(array)) {
        return NJS_DECLINED;
    }

    prop = &pq->scratch;

    if (pq->query == NJS_PROPERTY_QUERY_GET) {
        njs_set_number(njs_prop_value(prop),
                       njs_typed_array_prop(array, index));
        prop->type = NJS_PROPERTY;

    } else {
        njs_prop_typed_ref(prop) = array;
        njs_prop_magic32(prop) = index;
        prop->type = NJS_PROPERTY_TYPED_ARRAY_REF;
    }

    prop->writable = 1;
    prop->enumerable = 1;
    prop->configurable = 0;

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
        (void) njs_string_slice(vm, njs_prop_value(prop), &string, &slice);

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
    njs_value_t *value)
{
    njs_object_prop_t   *prop;
    njs_exotic_slots_t  *slots;

    slots = njs_object_slots(value);

    if (njs_slow_path(slots->prop_handler == NULL)) {
        return NJS_DECLINED;
    }

    pq->temp = 1;
    prop = &pq->scratch;

    njs_memzero(prop, sizeof(njs_object_prop_t));

    /*
     * njs_memzero() does also:
     *   prop->type = NJS_PROPERTY;
     *   prop->writable = 0;
     *   prop->configurable = 0;
     */

    njs_prop_magic32(prop) = slots->magic32;
    prop->name = pq->key;

    pq->lhq.value = prop;

    prop->writable = slots->writable;
    prop->configurable = slots->configurable;
    prop->enumerable = slots->enumerable;

    switch (pq->query) {

    case NJS_PROPERTY_QUERY_GET:
        return slots->prop_handler(vm, prop, value, NULL, njs_prop_value(prop));

    case NJS_PROPERTY_QUERY_SET:
        if (slots->writable == 0) {
            return NJS_OK;
        }

        break;

    case NJS_PROPERTY_QUERY_DELETE:
        if (slots->configurable == 0) {
            return NJS_OK;
        }

        break;
    }

    prop->type = NJS_PROPERTY_HANDLER;
    njs_prop_handler(prop) = slots->prop_handler;

    return NJS_OK;
}


njs_int_t
njs_value_property(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *retval)
{
    double                num;
    uint32_t              index;
    njs_int_t             ret;
    njs_array_t          *array;
    njs_object_prop_t     *prop;
    njs_typed_array_t     *tarray;
    njs_property_query_t  pq;

    njs_assert(njs_is_index_or_key(key));

    if (njs_fast_path(njs_is_number(key))) {
        num = njs_number(key);

        if (njs_slow_path(!njs_number_is_integer_index(num))) {
            goto slow_path;
        }

        index = (uint32_t) num;

        if (njs_is_typed_array(value)) {
            tarray = njs_typed_array(value);

            if (njs_slow_path(njs_is_detached_buffer(tarray->buffer))) {
                njs_type_error(vm, "detached buffer");
                return NJS_ERROR;
            }

            if (njs_slow_path(index >= njs_typed_array_length(tarray))) {
                goto slow_path;
            }

            njs_set_number(retval, njs_typed_array_prop(tarray, index));

            return NJS_OK;
        }

        if (njs_slow_path(!(njs_is_object(value)
                            && njs_object(value)->fast_array)))
        {
            goto slow_path;
        }

        /* njs_is_fast_array() */

        array = njs_array(value);

        if (njs_slow_path(index >= array->length
                          || !njs_is_valid(&array->start[index])))
        {
            goto slow_path;
        }

        njs_value_assign(retval, &array->start[index]);

        return NJS_OK;
    }

slow_path:

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0, 0);

    ret = njs_property_query(vm, &pq, value, key);

    switch (ret) {

    case NJS_OK:
        prop = pq.lhq.value;

        switch (prop->type) {
        case NJS_PROPERTY:
        case NJS_ACCESSOR:
            if (njs_is_data_descriptor(prop)) {
                njs_value_assign(retval, njs_prop_value(prop));
                break;
            }

            if (njs_prop_getter(prop) == NULL) {
                njs_set_undefined(retval);
                break;
            }

            return njs_function_apply(vm, njs_prop_getter(prop), value, 1,
                                      retval);

        case NJS_PROPERTY_HANDLER:
            pq.scratch = *prop;
            prop = &pq.scratch;
            ret = njs_prop_handler(prop)(vm, prop, value, NULL,
                                         njs_prop_value(prop));

            if (njs_slow_path(ret != NJS_OK)) {
                if (ret == NJS_ERROR) {
                    return ret;
                }

                njs_set_undefined(njs_prop_value(prop));
            }

            njs_value_assign(retval, njs_prop_value(prop));

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

        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_int_t
njs_value_property_set(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *setval)
{
    double                num;
    uint32_t              index;
    njs_int_t             ret;
    njs_array_t           *array;
    njs_object_prop_t     *prop;
    njs_typed_array_t     *tarray;
    njs_property_query_t  pq;

    static const njs_str_t  length_key = njs_str("length");

    njs_assert(njs_is_index_or_key(key));

    if (njs_fast_path(njs_is_number(key))) {
        num = njs_number(key);

        if (njs_slow_path(!njs_number_is_integer_index(num))) {
            goto slow_path;
        }

        index = (uint32_t) num;

        if (njs_is_typed_array(value)) {
            tarray = njs_typed_array(value);

            if (njs_fast_path(index < njs_typed_array_length(tarray))) {
                return njs_typed_array_set_value(vm, tarray, index, setval);
            }

            return NJS_OK;
        }

        if (njs_slow_path(!(njs_is_object(value)
                            && njs_object(value)->fast_array)))
        {
            goto slow_path;
        }

        /* NJS_ARRAY */

        array = njs_array(value);

        if (njs_slow_path(index >= array->length)) {
            goto slow_path;
        }

        njs_value_assign(&array->start[index], setval);

        return NJS_OK;
    }

slow_path:

    if (njs_is_primitive(value)) {
        njs_type_error(vm, "property set on primitive %s type",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 0, 0);

    ret = njs_property_query(vm, &pq, value, key);

    switch (ret) {

    case NJS_OK:
        prop = pq.lhq.value;

        if (njs_is_data_descriptor(prop)) {
            if (!prop->writable) {
                njs_key_string_get(vm, &pq.key,  &pq.lhq.key);
                njs_type_error(vm,
                             "Cannot assign to read-only property \"%V\" of %s",
                               &pq.lhq.key, njs_type_string(value->type));
                return NJS_ERROR;
            }

        } else {
            if (njs_prop_setter(prop) != NULL) {
                return njs_function_call(vm, njs_prop_setter(prop),
                                         value, setval, 1, &vm->retval);
            }

            njs_key_string_get(vm, &pq.key,  &pq.lhq.key);
            njs_type_error(vm,
                     "Cannot set property \"%V\" of %s which has only a getter",
                           &pq.lhq.key, njs_type_string(value->type));
            return NJS_ERROR;
        }

        if (prop->type == NJS_PROPERTY_HANDLER) {
            ret = njs_prop_handler(prop)(vm, prop, value, setval, &vm->retval);
            if (njs_slow_path(ret != NJS_DECLINED)) {
                return ret;
            }
        }

        if (pq.own) {
            switch (prop->type) {
            case NJS_PROPERTY:
                if (njs_is_array(value)) {
                    if (njs_slow_path(pq.lhq.key_hash == NJS_LENGTH_HASH)) {
                        if (njs_strstr_eq(&pq.lhq.key, &length_key)) {
                            return njs_array_length_set(vm, value, prop, setval);
                        }
                    }
                }

                goto found;

            case NJS_PROPERTY_REF:
            case NJS_PROPERTY_PLACE_REF:
                njs_value_assign(njs_prop_ref(prop), setval);
                return NJS_OK;

            case NJS_PROPERTY_TYPED_ARRAY_REF:
                return njs_typed_array_set_value(vm,
                                         njs_typed_array(njs_prop_value(prop)),
                                         njs_prop_magic32(prop),
                                         setval);

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
            if (!njs_object(value)->extensible) {
                goto fail;
            }

            prop = pq.own_whiteout;

            prop->type = NJS_PROPERTY;
            prop->enumerable = 1;
            prop->configurable = 1;
            prop->writable = 1;

            goto found;
        }

        if (njs_slow_path(pq.own && njs_is_typed_array(value)
                          && njs_is_string(key)))
        {
            /* Integer-Indexed Exotic Objects [[DefineOwnProperty]]. */
            if (!isnan(njs_string_to_index(key))) {
                return NJS_OK;
            }
        }

        break;

    case NJS_ERROR:
    default:

        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_object(value)->extensible)) {
        goto fail;
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

    njs_value_assign(njs_prop_value(prop), setval);

    return NJS_OK;

fail:

    njs_key_string_get(vm, &pq.key, &pq.lhq.key);
    njs_type_error(vm, "Cannot add property \"%V\", object is not extensible",
                   &pq.lhq.key);

    return NJS_ERROR;
}


njs_int_t
njs_value_property_delete(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *removed, njs_bool_t thrw)
{
    double                num;
    uint32_t              index;
    njs_int_t             ret;
    njs_array_t           *array;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    njs_assert(njs_is_index_or_key(key));

    if (njs_fast_path(njs_is_number(key))) {
        if (njs_slow_path(!(njs_is_fast_array(value)))) {
            goto slow_path;
        }

        num = njs_number(key);

        if (njs_slow_path(!njs_number_is_integer_index(num))) {
            goto slow_path;
        }

        index = (uint32_t) num;

        array = njs_array(value);

        if (njs_slow_path(index >= array->length)) {
            goto slow_path;
        }

        njs_value_assign(&array->start[index], &njs_value_invalid);

        return NJS_OK;
    }

slow_path:

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_DELETE, 0, 1);

    ret = njs_property_query(vm, &pq, value, key);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    prop = pq.lhq.value;

    if (njs_slow_path(!prop->configurable)) {
        if (thrw) {
            njs_key_string_get(vm, &pq.key,  &pq.lhq.key);
            njs_type_error(vm, "Cannot delete property \"%V\" of %s",
                           &pq.lhq.key, njs_type_string(value->type));
            return NJS_ERROR;
        }

        return NJS_OK;
    }

    switch (prop->type) {
    case NJS_PROPERTY_HANDLER:
        if (njs_is_object(value) && njs_object_slots(value) != NULL) {
            ret = njs_prop_handler(prop)(vm, prop, value, NULL, NULL);
            if (njs_slow_path(ret != NJS_DECLINED)) {
                return ret;
            }
        }

        /* Fall through. */

    case NJS_PROPERTY:
        break;

    case NJS_ACCESSOR:
        if (removed == NULL) {
            break;
        }

        if (njs_prop_getter(prop) == NULL) {
            njs_set_undefined(removed);
            break;
        }

        return njs_function_apply(vm, njs_prop_getter(prop), value, 1, removed);

    case NJS_PROPERTY_REF:
    case NJS_PROPERTY_PLACE_REF:
        if (removed != NULL) {
            njs_value_assign(removed, njs_prop_ref(prop));
        }

        njs_set_invalid(njs_prop_ref(prop));
        return NJS_OK;

    default:
        njs_internal_error(vm, "unexpected property type \"%s\" "
                           "while deleting", njs_prop_type_string(prop->type));
        return NJS_ERROR;
    }

    /* GC: release value. */
    if (removed != NULL) {
        njs_value_assign(removed, njs_prop_value(prop));
    }

    prop->type = NJS_WHITEOUT;
    njs_set_invalid(njs_prop_value(prop));

    return NJS_OK;
}


njs_int_t
njs_primitive_value_to_string(njs_vm_t *vm, njs_value_t *dst,
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
        njs_symbol_conversion_failed(vm, 1);
        return NJS_ERROR;

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


njs_int_t
njs_primitive_value_to_chain(njs_vm_t *vm, njs_chb_t *chain,
    const njs_value_t *src)
{
    njs_string_prop_t  string;

    switch (src->type) {

    case NJS_NULL:
        njs_chb_append_literal(chain, "null");
        return njs_length("null");

    case NJS_UNDEFINED:
        njs_chb_append_literal(chain, "undefined");
        return njs_length("undefined");

    case NJS_BOOLEAN:
        if (njs_is_true(src)) {
            njs_chb_append_literal(chain, "true");
            return njs_length("true");

        } else {
            njs_chb_append_literal(chain, "false");
            return njs_length("false");
        }

    case NJS_NUMBER:
        return njs_number_to_chain(vm, chain, njs_number(src));

    case NJS_SYMBOL:
        njs_symbol_conversion_failed(vm, 1);
        return NJS_ERROR;

    case NJS_STRING:
        (void) njs_string_prop(&string, src);
        njs_chb_append(chain, string.start, string.size);
        return string.length;

    default:
        return NJS_ERROR;
    }
}


njs_int_t
njs_value_to_object(njs_vm_t *vm, njs_value_t *value)
{
    njs_uint_t          index;
    njs_object_value_t  *object;

    if (njs_slow_path(njs_is_null_or_undefined(value))) {
        njs_type_error(vm, "cannot convert null or undefined to object");
        return NJS_ERROR;
    }

    if (njs_fast_path(njs_is_object(value))) {
        return NJS_OK;
    }

    if (njs_is_primitive(value)) {
        index = njs_primitive_prototype_index(value->type);
        object = njs_object_value_alloc(vm, index, 0, value);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        njs_set_object_value(value, object);

        return NJS_OK;
    }

    njs_type_error(vm, "cannot convert %s to object",
                   njs_type_string(value->type));

    return NJS_ERROR;
}


void
njs_symbol_conversion_failed(njs_vm_t *vm, njs_bool_t to_string)
{
    njs_type_error(vm, to_string
        ? "Cannot convert a Symbol value to a string"
        : "Cannot convert a Symbol value to a number");
}


njs_int_t
njs_value_species_constructor(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *default_constructor, njs_value_t *dst)
{
    njs_int_t    ret;
    njs_value_t  constructor, retval;

    static const njs_value_t  string_constructor = njs_string("constructor");
    static const njs_value_t  string_species =
                                njs_wellknown_symbol(NJS_SYMBOL_SPECIES);

    ret = njs_value_property(vm, object, njs_value_arg(&string_constructor),
                             &constructor);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_is_undefined(&constructor)) {
        goto default_constructor;
    }

    if (njs_slow_path(!njs_is_object(&constructor))) {
        njs_type_error(vm, "constructor is not object");
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, &constructor, njs_value_arg(&string_species),
                             &retval);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_value_is_null_or_undefined(&retval)) {
        goto default_constructor;
    }

    if (!njs_is_function(&retval)) {
        njs_type_error(vm, "object does not contain a constructor");
        return NJS_ERROR;
    }

    *dst = retval;

    return NJS_OK;

default_constructor:

    *dst = *default_constructor;

    return NJS_OK;
}


njs_int_t
njs_value_method(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *retval)
{
    njs_int_t  ret;

    ret = njs_value_to_object(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_property(vm, value, key, retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return (ret == NJS_DECLINED) ? NJS_OK : ret;
    }

    if (njs_slow_path(!njs_is_function(retval))) {
        njs_type_error(vm, "method is not callable");
        return NJS_ERROR;
    }

    return NJS_OK;
}
