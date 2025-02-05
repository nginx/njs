
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>

const njs_value_t  njs_value_null =         njs_value(NJS_NULL, 0, 0.0);
const njs_value_t  njs_value_undefined =    njs_value(NJS_UNDEFINED, 0, NAN);
const njs_value_t  njs_value_false =        njs_value(NJS_BOOLEAN, 0, 0.0);
const njs_value_t  njs_value_true =         njs_value(NJS_BOOLEAN, 1, 1.0);
const njs_value_t  njs_value_zero =         njs_value(NJS_NUMBER, 0, 0.0);
const njs_value_t  njs_value_nan =          njs_value(NJS_NUMBER, 0, NAN);
const njs_value_t  njs_value_invalid =      njs_value(NJS_INVALID, 0, 0.0);

/*
 * A hint value is 0 for numbers and 1 for strings.  The value chooses
 * method calls order specified by ECMAScript 5.1: "valueOf", "toString"
 * for numbers and "toString", "valueOf" for strings.
 */

njs_int_t
njs_value_to_primitive(njs_vm_t *vm, njs_value_t *dst, njs_value_t *value,
    njs_uint_t hint)
{
    njs_int_t                ret;
    njs_uint_t               tries;
    njs_value_t              method, retval;
    njs_flathsh_obj_query_t  lhq;

    static const njs_value_t *hashes[] = {
        &njs_atom.vs_valueOf,
        &njs_atom.vs_toString,
    };

    if (njs_is_primitive(value)) {
        *dst = *value;
        return NJS_OK;
    }

    tries = 0;
    lhq.proto = &njs_object_hash_proto;

    for ( ;; ) {
        ret = NJS_ERROR;

        if (njs_is_object(value) && tries < 2) {
            hint ^= tries++;

            lhq.key_hash = hashes[hint]->atom_id;

            ret = njs_object_property(vm, njs_object(value), &lhq, &method);

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
                ret = njs_value_property(vm, value, k++, dst++);
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

                ret = njs_value_property(vm, value, k, &entry->start[1]);
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

    ret = njs_value_property(vm, value, njs_value_arg(&njs_atom.vs_valueOf),
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

        if (pq->key.atom_id == 0) {
            ret = njs_atom_atomize_key(vm, &pq->key);
            if (ret != NJS_OK) {
                return ret;
            }
        }
        pq->lhq.key_hash = pq->key.atom_id;

        ret = njs_flathsh_obj_find(&array->object.hash, &pq->lhq);
        if (ret == NJS_OK) {
            prop = pq->lhq.value;

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

    pq->lhq.value = prop;

    return NJS_OK;
}


njs_int_t
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


njs_int_t
njs_string_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_value_t *object, uint32_t index)
{
    njs_int_t          ret;
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
            ret = njs_uint32_to_string(vm, &pq->key, index);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            njs_string_get(&pq->key, &pq->lhq.key);
        }

        return NJS_OK;
    }

    return NJS_DECLINED;
}


njs_int_t
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

    pq->lhq.value = prop;

    prop->writable = slots->writable;
    prop->configurable = slots->configurable;
    prop->enumerable = slots->enumerable;

    switch (pq->query) {

    case NJS_PROPERTY_QUERY_GET:
        return slots->prop_handler(vm, prop, pq->lhq.key_hash, value, NULL,
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
            ret = njs_prop_handler(prop)(vm, prop, pq.lhq.key_hash, value, NULL,
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
    double                    num;
    uint32_t                  index;
    njs_int_t                 ret;
    njs_array_t               *array;
    njs_value_t               retval;
    njs_object_prop_t         *prop;
    njs_typed_array_t         *tarray;
    njs_property_query_t      pq;
    njs_flathsh_obj_elt_t     *elt;
    njs_flathsh_obj_descr_t   *h;

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
                                         value, setval, 1, &retval);
            }

            njs_key_string_get(vm, &pq.key,  &pq.lhq.key);
            njs_type_error(vm,
                     "Cannot set property \"%V\" of %s which has only a getter",
                           &pq.lhq.key, njs_type_string(value->type));
            return NJS_ERROR;
        }

        if (prop->type == NJS_PROPERTY_HANDLER) {
            ret = njs_prop_handler(prop)(vm, prop, pq.lhq.key_hash, value,
                                         setval, &retval);
            if (njs_slow_path(ret != NJS_DECLINED)) {
                return ret;
            }
        }

        if (pq.own) {
            switch (prop->type) {
            case NJS_PROPERTY:
                if (njs_is_array(value)) {
                    if (njs_slow_path(pq.lhq.key_hash ==
                                      njs_atom.vs_length.atom_id)) {
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

            /*
             * Previously deleted property.
             *
             * delete it, and then
             * insert it again as new one to preserve insertion order.
             */

            if (!njs_object(value)->extensible) {
                goto fail;
            }

            pq.lhq.pool = vm->mem_pool;

            int rc = njs_flathsh_obj_delete(pq.own_whiteout, &pq.lhq);
            if (rc != NJS_OK) {
                return NJS_ERROR;
            }

            h = pq.own_whiteout->slot;

            if (h == NULL) {
                h = njs_flathsh_obj_new(&pq.lhq);
                if (njs_slow_path(h == NULL)) {
                    return NJS_ERROR;
                }

                pq.own_whiteout->slot = h;
            }

            elt = njs_flathsh_obj_add_elt(pq.own_whiteout, &pq.lhq);
            if (njs_slow_path(elt == NULL)) {
                return NJS_ERROR;
            }

            elt->value = (&pq.lhq)->value;

            prop = (njs_object_prop_t *) elt->value;

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

    if (!pq.key.atom_id) {
        ret = njs_atom_atomize_key(vm, &pq.key);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
    }

    prop = njs_object_prop_alloc(vm, &njs_value_undefined, 1);
    if (njs_slow_path(prop == NULL)) {
        return NJS_ERROR;
    }

    pq.lhq.replace = 0;
    pq.lhq.value = prop;

    pq.lhq.key_hash = pq.key.atom_id;

    pq.lhq.pool = vm->mem_pool;

    ret = njs_flathsh_obj_insert2(njs_object_hash(value), &pq.lhq);
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
            ret = njs_prop_handler(prop)(vm, prop, pq.lhq.key_hash, value, NULL,
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
    prop->enum_in_object_hash = 1;

    return NJS_OK;
}


njs_int_t
njs_primitive_value_to_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *src)
{
    const njs_value_t  *value;

    switch (src->type) {

    case NJS_NULL:
        value = &njs_atom.vs_null;
        break;

    case NJS_UNDEFINED:
        value = &njs_atom.vs_undefined;
        break;

    case NJS_BOOLEAN:
        value = njs_is_true(src) ? &njs_atom.vs_true : &njs_atom.vs_false;
        break;

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
        (void) njs_string_prop(&string, src);
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

    ret = njs_value_property(vm, object, njs_value_arg(&njs_atom.vs_constructor),
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

    ret = njs_value_property(vm, &constructor,
                             njs_value_arg(&njs_atom.vw_species), &retval);
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
