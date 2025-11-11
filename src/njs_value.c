
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


njs_inline njs_int_t
njs_object_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *object, uint32_t atom_id);
static njs_int_t njs_array_property_query(njs_vm_t *vm,
    njs_property_query_t *pq, njs_array_t *array, uint32_t index,
    uint32_t atom_id);
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


njs_int_t
njs_value_to_primitive(njs_vm_t *vm, njs_value_t *dst, njs_value_t *value,
    njs_uint_t hint)
{
    njs_int_t            ret;
    njs_uint_t           tries, force_ordinary;
    njs_value_t          method, retval, arguments[2];
    njs_flathsh_query_t  fhq;

    static const uint32_t atoms[] = {
        NJS_ATOM_STRING_valueOf,
        NJS_ATOM_STRING_toString,
    };

    static const njs_uint_t atom_by_hint[] = {
        NJS_ATOM_STRING_number,
        NJS_ATOM_STRING_string,
        NJS_ATOM_STRING_default,
    };

    if (njs_is_primitive(value)) {
        *dst = *value;
        return NJS_OK;
    }

    tries = 0;
    fhq.proto = &njs_object_hash_proto;

    if (!njs_is_object(value)) {
        goto error;
    }

    force_ordinary = hint & NJS_HINT_FORCE_ORDINARY;
    hint &= ~NJS_HINT_FORCE_ORDINARY;

    if (force_ordinary) {
        goto ordinary;
    }

    fhq.key_hash = NJS_ATOM_SYMBOL_toPrimitive;

    ret = njs_object_property(vm, njs_object(value), &fhq, &method);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_is_function(&method)) {
        arguments[0] = *value;

        njs_atom_to_value(vm, &arguments[1], atom_by_hint[hint]);

        ret = njs_function_apply(vm, njs_function(&method), arguments, 2,
                                 &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_primitive(&retval)) {
            goto done;
        }

        goto error;
    }

ordinary:

    if (hint != NJS_HINT_STRING) {
        hint = NJS_HINT_NUMBER;
    }

    for ( ;; ) {
        ret = NJS_ERROR;

        if (tries < 2) {
            hint ^= tries++;

            fhq.key_hash = atoms[hint];

            ret = njs_object_property(vm, njs_object(value), &fhq, &method);
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
                    goto done;
                }
            }

            /* Try the second method. */
            continue;
        }

error:

        njs_type_error(vm, "Cannot convert object to primitive value");

        return NJS_ERROR;
    }

done:

    *dst = retval;

    return NJS_OK;
}


njs_array_t *
njs_value_enumerate(njs_vm_t *vm, njs_value_t *value, uint32_t flags)
{
    njs_int_t           ret;
    njs_value_t         keys;
    njs_object_value_t  obj_val;
    njs_exotic_slots_t  *slots;

    if (njs_is_object(value)) {
        if ((flags & NJS_ENUM_KEYS) && (flags & NJS_ENUM_STRING)) {
            slots = njs_object_slots(value);
            if (slots != NULL && slots->keys != NULL) {
                ret = slots->keys(vm, value, &keys);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NULL;
                }

                return njs_array(&keys);
            }
        }

        return njs_object_enumerate(vm, njs_object(value), flags);
    }

    if (value->type != NJS_STRING) {
        return njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
    }

    obj_val.object = vm->string_object;
    obj_val.value = *value;

    return njs_object_enumerate(vm, (njs_object_t *) &obj_val, flags);
}


njs_array_t *
njs_value_own_enumerate(njs_vm_t *vm, njs_value_t *value, uint32_t flags)
{
    njs_int_t           ret, len;
    njs_array_t         *values, *entry;
    njs_value_t         keys, *k, *dst, *end;
    njs_object_value_t  obj_val;
    njs_exotic_slots_t  *slots;

    if (njs_is_object(value)) {
        slots = njs_object_slots(value);
        if (slots == NULL || slots->keys == NULL) {
            return njs_object_own_enumerate(vm, njs_object(value), flags);
        }

        ret = slots->keys(vm, value, &keys);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        switch (njs_object_enum_kind(flags)) {
        case NJS_ENUM_KEYS:
            return njs_array(&keys);

        case NJS_ENUM_VALUES:
            len = njs_array_len(&keys);
            k = njs_array(&keys)->start;
            end = k + len;

            values = njs_array_alloc(vm, 1, len, 0);
            if (njs_slow_path(values == NULL)) {
                return NULL;
            }

            dst = values->start;

            while (k < end) {
                ret = njs_value_property_val(vm, value, k++, dst++);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NULL;
                }
            }

            return values;

        case NJS_ENUM_BOTH:
        default:
            len = njs_array_len(&keys);
            k = njs_array(&keys)->start;
            end = k + len;

            values = njs_array_alloc(vm, 1, len, 0);
            if (njs_slow_path(values == NULL)) {
                return NULL;
            }

            dst = values->start;

            while (k < end) {
                entry = njs_array_alloc(vm, 1, 2, 0);
                if (njs_slow_path(entry == NULL)) {
                    return NULL;
                }

                ret = njs_value_property_val(vm, value, k, &entry->start[1]);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NULL;
                }

                njs_value_assign(&entry->start[0], k++);

                njs_set_array(dst++, entry);
            }

            return values;
        }
    }

    if (value->type != NJS_STRING) {
        return njs_array_alloc(vm, 1, 0, NJS_ARRAY_SPARE);
    }

    obj_val.object = vm->string_object;
    obj_val.value = *value;

    return njs_object_own_enumerate(vm, (njs_object_t *) &obj_val, flags);
}


njs_int_t
njs_value_of(njs_vm_t *vm, njs_value_t *value, njs_value_t *retval)
{

    njs_int_t  ret;

    if (njs_slow_path(!njs_is_object(value))) {
        return NJS_DECLINED;
    }

    ret = njs_value_property(vm, value, NJS_ATOM_STRING_valueOf, retval);
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
        *length = njs_string_prop(vm, &string_prop, value);

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


void
njs_value_function_set(njs_value_t *value, njs_function_t *function)
{
    njs_set_function(value, function);
}


void
njs_value_external_set(njs_value_t *value, njs_external_ptr_t external)
{
    njs_assert(njs_value_is_external(value, NJS_PROTO_ID_ANY));

    njs_object_data(value) = external;
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


njs_function_native_t
njs_value_native_function(const njs_value_t *value)
{
    njs_function_t  *function;

    if (njs_is_function(value)) {
        function = njs_function(value);

        if (function->native) {
            return function->u.native;
        }
    }

    return NULL;
}


void *
njs_value_ptr(const njs_value_t *value)
{
    return njs_data(value);
}


njs_external_ptr_t
njs_value_external(const njs_value_t *value)
{
    njs_assert(njs_value_is_external(value, NJS_PROTO_ID_ANY));

    return njs_object_data(value);
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
njs_value_is_error(const njs_value_t *value)
{
    return njs_is_error(value);
}


njs_int_t
njs_value_is_external(const njs_value_t *value, njs_int_t proto_id)
{
    return njs_is_object_data(value, njs_make_tag(proto_id));
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


njs_int_t
njs_value_is_data_view(const njs_value_t *value)
{
    return njs_is_data_view(value);
}


njs_int_t
njs_value_is_promise(const njs_value_t *value)
{
    return njs_is_promise(value);
}


/*
 * ES5.1, 8.12.1: [[GetOwnProperty]], [[GetProperty]].
 * The njs_property_query() returns values
 *   NJS_OK               property has been found in object,
 *     retval of type njs_object_prop_t * is in pq->fhq.value.
 *     in NJS_PROPERTY_QUERY_GET
 *       prop->type is NJS_PROPERTY or NJS_PROPERTY_HANDLER.
 *     in NJS_PROPERTY_QUERY_SET, NJS_PROPERTY_QUERY_DELETE
 *       prop->type is NJS_PROPERTY, NJS_PROPERTY_REF, NJS_PROPERTY_PLACE_REF,
 *       NJS_PROPERTY_TYPED_ARRAY_REF or
 *       NJS_PROPERTY_HANDLER.
 *   NJS_DECLINED         property was not found in object,
 *     if pq->fhq.value != NULL it contains retval of type
 *     njs_object_prop_t * where prop->type is NJS_WHITEOUT
 *   NJS_ERROR            exception has been thrown.
 */

njs_int_t
njs_property_query(njs_vm_t *vm, njs_property_query_t *pq, njs_value_t *value,
    uint32_t atom_id)
{
    uint32_t        index;
    njs_int_t       ret;
    njs_object_t    *obj;

    njs_assert(atom_id != NJS_ATOM_STRING_unknown);

    switch (value->type) {
    case NJS_BOOLEAN:
    case NJS_NUMBER:
    case NJS_SYMBOL:
        index = njs_primitive_prototype_index(value->type);
        obj = njs_vm_proto(vm, index);
        break;

    case NJS_STRING:
        if (njs_atom_is_number(atom_id)) {
            return njs_string_property_query(vm, pq, value,
                                             njs_atom_number(atom_id));
        }

        obj = &vm->string_object;
        break;

    case NJS_OBJECT:
    case NJS_ARRAY:
    case NJS_FUNCTION:
    case NJS_ARRAY_BUFFER:
    case NJS_DATA_VIEW:
    case NJS_TYPED_ARRAY:
    case NJS_REGEXP:
    case NJS_DATE:
    case NJS_PROMISE:
    case NJS_OBJECT_VALUE:
        obj = njs_object(value);
        break;

    case NJS_UNDEFINED:
    case NJS_NULL:
    default:
        njs_atom_string_get(vm, atom_id, &pq->fhq.key);
        njs_type_error(vm, "cannot get property \"%V\" of %s", &pq->fhq.key,
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    ret = njs_object_property_query(vm, pq, obj, atom_id);

    if (njs_slow_path(ret == NJS_DECLINED && obj->slots != NULL)) {
        return njs_external_property_query(vm, pq, value);
    }

    return ret;
}


njs_inline njs_int_t
njs_object_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_object_t *object, uint32_t atom_id)
{
    double              num;
    njs_int_t           ret;
    njs_bool_t          own;
    njs_value_t         key;
    njs_array_t         *array;
    njs_object_t        *proto;
    njs_object_prop_t   *prop;
    njs_typed_array_t   *tarray;
    njs_object_value_t  *ov;

    pq->fhq.proto = &njs_object_hash_proto;

    own = pq->own;
    pq->own = 1;

    proto = object;

    do {
        switch (proto->type) {
        case NJS_ARRAY:
            array = (njs_array_t *) proto;

            if (njs_fast_path(njs_atom_is_number(atom_id))) {
                ret = njs_array_property_query(vm, pq, array,
                                               njs_atom_number(atom_id), atom_id);
                if (njs_fast_path(ret != NJS_DECLINED)) {
                    return (ret == NJS_DONE) ? NJS_DECLINED : ret;
                }
            }

            ret = njs_atom_to_value(vm, &key, atom_id);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            num = njs_key_to_index(&key);

            if (njs_key_is_integer_index(num, &key)) {
                ret = njs_array_property_query(vm, pq, array, num, atom_id);
                if (njs_fast_path(ret != NJS_DECLINED)) {
                    return (ret == NJS_DONE) ? NJS_DECLINED : ret;
                }
            }

            break;

        case NJS_TYPED_ARRAY:
            if (njs_fast_path(njs_atom_is_number(atom_id))) {
                tarray = (njs_typed_array_t *) proto;
                return njs_typed_array_property_query(vm, pq, tarray,
                                                      njs_atom_number(atom_id));
            }

            ret = njs_atom_to_value(vm, &key, atom_id);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            num = njs_key_to_index(&key);

            if (!isnan(num)) {
                return NJS_DECLINED;
            }

            break;

        case NJS_OBJECT_VALUE:
            ov = (njs_object_value_t *) proto;
            if (!njs_is_string(&ov->value)) {
                break;
            }

            if (njs_fast_path(njs_atom_is_number(atom_id))) {
                ov = (njs_object_value_t *) proto;
                ret = njs_string_property_query(vm, pq, &ov->value,
                                                njs_atom_number(atom_id));
                if (njs_fast_path(ret != NJS_DECLINED)) {
                    return ret;
                }
            }

            break;

        default:
            break;
        }

        pq->fhq.key_hash = atom_id;

        ret = njs_flathsh_unique_find(&proto->hash, &pq->fhq);

        if (ret == NJS_OK) {
            prop = pq->fhq.value;

            if (prop->type != NJS_WHITEOUT) {
                return ret;
            }

            if (pq->own) {
                pq->own_whiteout = &proto->hash;
            }

        } else {
            ret = njs_flathsh_unique_find(&proto->shared_hash, &pq->fhq);
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
    njs_array_t *array, uint32_t index, uint32_t atom_id)
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

        pq->fhq.key_hash = atom_id;

        ret = njs_flathsh_unique_find(&array->object.hash, &pq->fhq);
        if (ret == NJS_OK) {
            prop = pq->fhq.value;

            if (prop->type != NJS_WHITEOUT) {
                return NJS_OK;
            }

            if (pq->own) {
                pq->own_whiteout = &array->object.hash;
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

    prop->writable = 1;
    prop->enumerable = 1;
    prop->configurable = 1;

    pq->fhq.value = prop;

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

    pq->fhq.value = prop;

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
    slice.string_length = njs_string_prop(vm, &string, object);

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

        pq->fhq.value = prop;

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

    pq->fhq.value = prop;

    prop->type = NJS_PROPERTY;
    prop->writable = slots->writable;
    prop->configurable = slots->configurable;
    prop->enumerable = slots->enumerable;

    switch (pq->query) {

    case NJS_PROPERTY_QUERY_GET:
        return slots->prop_handler(vm, prop, pq->fhq.key_hash, value, NULL,
                                   njs_prop_value(prop));

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
njs_value_property(njs_vm_t *vm, njs_value_t *value, uint32_t atom_id,
    njs_value_t *retval)
{
    uint32_t              index;
    njs_int_t             ret;
    njs_array_t          *array;
    njs_object_prop_t     *prop;
    njs_typed_array_t     *tarray;
    njs_property_query_t  pq;

    if (njs_fast_path(njs_atom_is_number(atom_id))) {
        index = njs_atom_number(atom_id);

        if (njs_is_typed_array(value)) {
            tarray = njs_typed_array(value);

            if (njs_slow_path(njs_is_detached_buffer(tarray->buffer))) {
                goto not_found;
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

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0);

    ret = njs_property_query(vm, &pq, value, atom_id);

    switch (ret) {

    case NJS_OK:
        prop = pq.fhq.value;

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
            ret = njs_prop_handler(prop)(vm, prop, atom_id, value, NULL,
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
not_found:
        njs_set_undefined(retval);

        return NJS_DECLINED;

    case NJS_ERROR:
    default:

        return NJS_ERROR;
    }

    return NJS_OK;
}


njs_int_t
njs_value_property_set(njs_vm_t *vm, njs_value_t *value, uint32_t atom_id,
    njs_value_t *setval)
{
    uint32_t              index;
    njs_int_t             ret;
    njs_array_t           *array;
    njs_value_t           retval, key;
    njs_object_prop_t     *prop;
    njs_typed_array_t     *tarray;
    njs_flathsh_elt_t     *elt;
    njs_flathsh_descr_t   *h;
    njs_property_query_t  pq;

    if (njs_fast_path(njs_atom_is_number(atom_id))) {
        index = njs_atom_number(atom_id);

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

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_SET, 0);

    ret = njs_property_query(vm, &pq, value, atom_id);

    switch (ret) {

    case NJS_OK:
        prop = pq.fhq.value;

        if (njs_is_data_descriptor(prop)) {
            if (!prop->writable) {
                njs_atom_string_get(vm, atom_id, &pq.fhq.key);
                njs_type_error(vm,
                             "Cannot assign to read-only property \"%V\" of %s",
                               &pq.fhq.key, njs_type_string(value->type));
                return NJS_ERROR;
            }

        } else {
            if (njs_prop_setter(prop) != NULL) {
                return njs_function_call(vm, njs_prop_setter(prop),
                                         value, setval, 1, &retval);
            }

            njs_atom_string_get(vm, atom_id, &pq.fhq.key);
            njs_type_error(vm,
                     "Cannot set property \"%V\" of %s which has only a getter",
                           &pq.fhq.key, njs_type_string(value->type));
            return NJS_ERROR;
        }

        if (prop->type == NJS_PROPERTY_HANDLER) {
            ret = njs_prop_handler(prop)(vm, prop, atom_id, value,
                                         setval, &retval);
            if (njs_slow_path(ret != NJS_DECLINED)) {
                return ret;
            }
        }

        if (pq.own) {
            switch (prop->type) {
            case NJS_PROPERTY:
                if (njs_is_array(value)) {
                    if (njs_slow_path(atom_id == NJS_ATOM_STRING_length)) {
                        return njs_array_length_set(vm, value, prop, setval);
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

            /*
             * Previously deleted property.
             *
             * delete it, and then
             * insert it again as new one to preserve insertion order.
             */

            if (!njs_object(value)->extensible) {
                goto fail;
            }

            pq.fhq.pool = vm->mem_pool;

            int rc = njs_flathsh_unique_delete(pq.own_whiteout, &pq.fhq);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }

            h = pq.own_whiteout->slot;

            if (h == NULL) {
                h = njs_flathsh_new(&pq.fhq);
                if (njs_slow_path(h == NULL)) {
                    return NJS_ERROR;
                }

                pq.own_whiteout->slot = h;
            }

            elt = njs_flathsh_add_elt(pq.own_whiteout, &pq.fhq);
            if (njs_slow_path(elt == NULL)) {
                return NJS_ERROR;
            }

            prop = (njs_object_prop_t *) elt;
            prop->type = NJS_PROPERTY;
            prop->enumerable = 1;
            prop->configurable = 1;
            prop->writable = 1;

            goto found;
        }

        if (njs_slow_path(pq.own && njs_is_typed_array(value)
                          && !njs_atom_is_number(atom_id)))
        {
            /* Integer-Indexed Exotic Objects [[DefineOwnProperty]]. */

            ret = njs_atom_to_value(vm, &key, atom_id);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (!isnan(njs_string_to_index(&key))) {
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


    pq.fhq.replace = 0;
    pq.fhq.key_hash = atom_id;
    pq.fhq.pool = vm->mem_pool;

    ret = njs_flathsh_unique_insert(njs_object_hash(value), &pq.fhq);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_internal_error(vm, "flathsh insert failed");
        return NJS_ERROR;
    }

    prop = pq.fhq.value;
    prop->type = NJS_PROPERTY;
    prop->enumerable = 1;
    prop->configurable = 1;
    prop->writable = 1;

found:

    njs_value_assign(njs_prop_value(prop), setval);

    return NJS_OK;

fail:

    njs_atom_string_get(vm, atom_id, &pq.fhq.key);
    njs_type_error(vm, "Cannot add property \"%V\", object is not extensible",
                   &pq.fhq.key);

    return NJS_ERROR;
}


njs_int_t
njs_value_property_delete(njs_vm_t *vm, njs_value_t *value, uint32_t atom_id,
    njs_value_t *removed, njs_bool_t thrw)
{
    uint32_t              index;
    njs_int_t             ret;
    njs_array_t           *array;
    njs_object_prop_t     *prop;
    njs_property_query_t  pq;

    if (njs_fast_path(njs_atom_is_number(atom_id))) {
        if (njs_slow_path(!(njs_is_fast_array(value)))) {
            goto slow_path;
        }

        index = (uint32_t) njs_atom_number(atom_id);

        array = njs_array(value);

        if (njs_slow_path(index >= array->length)) {
            goto slow_path;
        }

        njs_value_assign(&array->start[index], &njs_value_invalid);

        return NJS_OK;
    }

slow_path:

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_DELETE, 1);

    ret = njs_property_query(vm, &pq, value, atom_id);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    prop = pq.fhq.value;

    if (njs_slow_path(!prop->configurable)) {
        if (thrw) {
            njs_atom_string_get(vm, atom_id, &pq.fhq.key);
            njs_type_error(vm, "Cannot delete property \"%V\" of %s",
                           &pq.fhq.key, njs_type_string(value->type));
            return NJS_ERROR;
        }

        return NJS_OK;
    }

    switch (prop->type) {
    case NJS_PROPERTY_HANDLER:
        if (njs_is_object(value) && njs_object_slots(value) != NULL) {
            ret = njs_prop_handler(prop)(vm, prop, atom_id, value, NULL,
                                         NULL);
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

    if (removed != NULL) {
        if (njs_is_valid(njs_prop_value(prop))) {
            njs_value_assign(removed, njs_prop_value(prop));

        } else {
            njs_set_undefined(removed);
        }
    }

    prop->type = NJS_WHITEOUT;

    return NJS_OK;
}


njs_int_t
njs_primitive_value_to_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *src)
{
    const njs_value_t  *value;

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
        return njs_number_to_string(vm, dst, src);

    case NJS_SYMBOL:
        njs_symbol_conversion_failed(vm, 1);
        return NJS_ERROR;

    case NJS_STRING:
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
        (void) njs_string_prop(vm, &string, src);
        njs_chb_append(chain, string.start, string.size);
        return string.length;

    default:
        return NJS_ERROR;
    }
}


njs_int_t
njs_value_to_integer(njs_vm_t *vm, njs_value_t *value, int64_t *dst)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, value, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    *dst = njs_number_to_integer(num);

    return NJS_OK;
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
njs_value_construct(njs_vm_t *vm, njs_value_t *constructor, njs_value_t *args,
    njs_uint_t nargs, njs_value_t *retval)
{
    njs_value_t   this;
    njs_object_t  *object;

    object = njs_function_new_object(vm, constructor);
    if (njs_slow_path(object == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&this, object);

    return njs_function_call2(vm, njs_function(constructor), &this, args,
                              nargs, retval, 1);
}


njs_int_t
njs_value_species_constructor(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *default_constructor, njs_value_t *dst)
{
    njs_int_t    ret;
    njs_value_t  constructor, retval;

    ret = njs_value_property(vm, object, NJS_ATOM_STRING_constructor,
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

    ret = njs_value_property(vm, &constructor, NJS_ATOM_SYMBOL_species,
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
njs_value_method(njs_vm_t *vm, njs_value_t *value, uint32_t atom_id,
    njs_value_t *retval)
{
    njs_int_t  ret;

    ret = njs_value_to_object(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_property(vm, value, atom_id, retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return (ret == NJS_DECLINED) ? NJS_OK : ret;
    }

    if (njs_slow_path(!njs_is_function(retval))) {
        njs_type_error(vm, "method is not callable");
        return NJS_ERROR;
    }

    return NJS_OK;
}
