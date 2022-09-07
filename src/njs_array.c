
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


#define njs_array_func(type)                                                  \
    ((type << 1) | NJS_ARRAY_FUNC)


#define njs_array_arg(type)                                                   \
    ((type << 1) | NJS_ARRAY_ARG)


#define njs_array_type(magic) (magic >> 1)
#define njs_array_arg1(magic) (magic & 0x1)


typedef enum {
    NJS_ARRAY_EVERY = 0,
    NJS_ARRAY_SOME,
    NJS_ARRAY_INCLUDES,
    NJS_ARRAY_INDEX_OF,
    NJS_ARRAY_FOR_EACH,
    NJS_ARRAY_FIND,
    NJS_ARRAY_FIND_INDEX,
    NJS_ARRAY_REDUCE,
    NJS_ARRAY_FILTER,
    NJS_ARRAY_MAP,
} njs_array_iterator_fun_t;


typedef enum {
    NJS_ARRAY_LAST_INDEX_OF = 0,
    NJS_ARRAY_REDUCE_RIGHT,
} njs_array_reverse_iterator_fun_t;


typedef enum {
    NJS_ARRAY_FUNC = 0,
    NJS_ARRAY_ARG
} njs_array_iterator_arg_t;


static njs_int_t njs_array_prototype_slice_copy(njs_vm_t *vm,
    njs_value_t *this, int64_t start, int64_t length);


njs_array_t *
njs_array_alloc(njs_vm_t *vm, njs_bool_t flat, uint64_t length, uint32_t spare)
{
    uint64_t     size;
    njs_int_t    ret;
    njs_array_t  *array;
    njs_value_t  value;

    if (njs_slow_path(length > UINT32_MAX)) {
        goto overflow;
    }

    array = njs_mp_alloc(vm->mem_pool, sizeof(njs_array_t));
    if (njs_slow_path(array == NULL)) {
        goto memory_error;
    }

    size = length + spare;

    if (flat || size <= NJS_ARRAY_LARGE_OBJECT_LENGTH) {
        array->data = njs_mp_align(vm->mem_pool, sizeof(njs_value_t),
                                   size * sizeof(njs_value_t));
        if (njs_slow_path(array->data == NULL)) {
            goto memory_error;
        }

    } else {
        array->data = NULL;
    }

    array->start = array->data;
    njs_lvlhsh_init(&array->object.hash);
    array->object.shared_hash = vm->shared->array_instance_hash;
    array->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_ARRAY].object;
    array->object.slots = NULL;
    array->object.type = NJS_ARRAY;
    array->object.shared = 0;
    array->object.extensible = 1;
    array->object.error_data = 0;
    array->object.fast_array = (array->data != NULL);

    if (njs_fast_path(array->object.fast_array)) {
        array->size = size;
        array->length = length;

    } else {
        array->size = 0;
        array->length = 0;

        njs_set_array(&value, array);
        ret = njs_array_length_redefine(vm, &value, length, 1);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }
    }

    return array;

memory_error:

    njs_memory_error(vm);

    return NULL;

overflow:

    njs_range_error(vm, "Invalid array length");

    return NULL;
}


void
njs_array_destroy(njs_vm_t *vm, njs_array_t *array)
{
    if (array->data != NULL) {
        njs_mp_free(vm->mem_pool, array->data);
    }

    /* TODO: destroy keys. */

    njs_mp_free(vm->mem_pool, array);
}


njs_int_t
njs_array_convert_to_slow_array(njs_vm_t *vm, njs_array_t *array)
{
    uint32_t           i, length;
    njs_value_t        index, value;
    njs_object_prop_t  *prop;

    if (njs_slow_path(!array->object.fast_array)) {
        return NJS_OK;
    }

    njs_set_array(&value, array);
    array->object.fast_array = 0;

    length = array->length;

    for (i = 0; i < length; i++) {
        if (njs_is_valid(&array->start[i])) {
            njs_uint32_to_string(&index, i);
            prop = njs_object_property_add(vm, &value, &index, 0);
            if (njs_slow_path(prop == NULL)) {
                return NJS_ERROR;
            }

            prop->value = array->start[i];
        }
    }

    /* GC: release value. */

    njs_mp_free(vm->mem_pool, array->data);
    array->start = NULL;

    return NJS_OK;
}


njs_int_t
njs_array_length_redefine(njs_vm_t *vm, njs_value_t *value, uint32_t length, int writable)
{
    njs_object_prop_t  *prop;

    static const njs_value_t  string_length = njs_string("length");

    if (njs_slow_path(!njs_is_array(value))) {
        njs_internal_error(vm, "njs_array_length_redefine() "
                           "applied to non-array");
        return NJS_ERROR;
    }

    prop = njs_object_property_add(vm, value, njs_value_arg(&string_length), 1);
    if (njs_slow_path(prop == NULL)) {
        njs_internal_error(vm, "njs_array_length_redefine() "
                           "cannot redefine \"length\"");
        return NJS_ERROR;
    }

    prop->writable = writable;
    prop->enumerable = 0;
    prop->configurable = 0;

    njs_value_number_set(&prop->value, length);

    return NJS_OK;
}


njs_int_t
njs_array_length_set(njs_vm_t *vm, njs_value_t *value,
    njs_object_prop_t *prev, njs_value_t *setval)
{
    double        num, idx;
    int64_t       prev_length;
    uint32_t      i, length;
    njs_int_t     ret;
    njs_array_t   *keys;

    ret = njs_value_to_number(vm, setval, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = (uint32_t) njs_number_to_length(num);
    if ((double) length != num) {
        njs_range_error(vm, "Invalid array length");
        return NJS_ERROR;
    }

    ret = njs_value_to_length(vm, &prev->value, &prev_length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    keys = NULL;

    if (length < prev_length) {
        keys = njs_array_indices(vm, value);
        if (njs_slow_path(keys == NULL)) {
            return NJS_ERROR;
        }

        if (keys->length != 0) {
            i = keys->length - 1;

            do {
                idx = njs_string_to_index(&keys->start[i]);
                if (idx >= length) {
                    ret = njs_value_property_delete(vm, value, &keys->start[i],
                                                    NULL, 1);
                    if (njs_slow_path(ret == NJS_ERROR)) {
                        goto done;
                    }
                }
            } while (i-- != 0);
        }
    }

    ret = njs_array_length_redefine(vm, value, length, prev->writable);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = NJS_OK;

done:

    if (keys != NULL) {
        njs_array_destroy(vm, keys);
    }

    return ret;
}


static njs_int_t
njs_array_copy_within(njs_vm_t *vm, njs_value_t *array, int64_t to_pos,
    int64_t from_pos, int64_t count, njs_bool_t forward)
{
    int64_t      i, from, to;
    njs_int_t    ret;
    njs_array_t  *arr;
    njs_value_t  value;

    if (njs_fast_path(njs_is_fast_array(array) && count > 0)) {
        arr = njs_array(array);

        memmove(&arr->start[to_pos], &arr->start[from_pos],
                count * sizeof(njs_value_t));

        return NJS_OK;
    }

    if (!forward) {
        from_pos += count - 1;
        to_pos += count - 1;
    }

    for (i = 0; i < count; i++) {
        if (forward) {
            from = from_pos + i;
            to = to_pos + i;

        } else {
            from = from_pos - i;
            to = to_pos - i;
        }

        ret = njs_value_property_i64(vm, array, from, &value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return NJS_ERROR;
        }

        if (ret == NJS_OK) {
            ret = njs_value_property_i64_set(vm, array, to, &value);

        } else {
            ret = njs_value_property_i64_delete(vm, array, to, NULL);
        }

        if (njs_slow_path(ret == NJS_ERROR)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


njs_int_t
njs_array_add(njs_vm_t *vm, njs_array_t *array, njs_value_t *value)
{
    njs_int_t  ret;

    ret = njs_array_expand(vm, array, 0, 1);

    if (njs_fast_path(ret == NJS_OK)) {
        /* GC: retain value. */
        array->start[array->length++] = *value;
    }

    return ret;
}


njs_int_t
njs_array_string_add(njs_vm_t *vm, njs_array_t *array, const u_char *start,
    size_t size, size_t length)
{
    njs_int_t  ret;

    ret = njs_array_expand(vm, array, 0, 1);

    if (njs_fast_path(ret == NJS_OK)) {
        return njs_string_new(vm, &array->start[array->length++], start, size,
                              length);
    }

    return ret;
}


njs_int_t
njs_array_expand(njs_vm_t *vm, njs_array_t *array, uint32_t prepend,
    uint32_t append)
{
    uint32_t     free_before, free_after;
    uint64_t     size;
    njs_value_t  *start, *old;

    njs_assert(array->object.fast_array);

    free_before = array->start - array->data;
    free_after = array->size - array->length - free_before;

    if (njs_fast_path(free_before >= prepend && free_after >= append)) {
        return NJS_OK;
    }

    size = (uint64_t) prepend + array->length + append;

    if (size < 16) {
        size *= 2;

    } else {
        size += size / 2;
    }

    if (njs_slow_path(size > (UINT32_MAX / sizeof(njs_value_t)))) {
        goto memory_error;
    }

    start = njs_mp_align(vm->mem_pool, sizeof(njs_value_t),
                         size * sizeof(njs_value_t));
    if (njs_slow_path(start == NULL)) {
        goto memory_error;
    }

    array->size = size;

    old = array->data;
    array->data = start;
    start += prepend;

    if (array->length != 0) {
        memcpy(start, array->start, array->length * sizeof(njs_value_t));
    }

    array->start = start;

    if (old != NULL) {
        njs_mp_free(vm->mem_pool, old);
    }

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_array_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double       num;
    uint32_t     size;
    njs_value_t  *value;
    njs_array_t  *array;

    args = &args[1];
    size = nargs - 1;

    if (size == 1 && njs_is_number(&args[0])) {
        num = njs_number(&args[0]);
        size = (uint32_t) njs_number_to_length(num);

        if ((double) size != num) {
            njs_range_error(vm, "Invalid array length");
            return NJS_ERROR;
        }

        args = NULL;
    }

    array = njs_array_alloc(vm, size <= NJS_ARRAY_FLAT_MAX_LENGTH,
                            size, NJS_ARRAY_SPARE);

    if (njs_fast_path(array != NULL)) {

        if (array->object.fast_array) {
            value = array->start;

            if (args == NULL) {
                while (size != 0) {
                    njs_set_invalid(value);
                    value++;
                    size--;
                }

            } else {
                while (size != 0) {
                    njs_retain(args);
                    *value++ = *args++;
                    size--;
                }
            }
        }

        njs_set_array(&vm->retval, array);

        return NJS_OK;
    }

    return NJS_ERROR;
}


static njs_int_t
njs_array_is_array(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    const njs_value_t  *value;

    if (nargs > 1 && njs_is_array(&args[1])) {
        value = &njs_value_true;

    } else {
        value = &njs_value_false;
    }

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_array_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t     length, i;
    njs_array_t  *array;

    length = nargs > 1 ? nargs - 1 : 0;

    array = njs_array_alloc(vm, 0, length, NJS_ARRAY_SPARE);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    njs_set_array(&vm->retval, array);

    if (array->object.fast_array) {
        for (i = 0; i < length; i++) {
            array->start[i] = args[i + 1];
        }
    }

    return NJS_OK;
}


static const njs_object_prop_t  njs_array_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Array"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isArray"),
        .value = njs_native_function(njs_array_is_array, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("of"),
        .value = njs_native_function(njs_array_of, 0),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_array_constructor_init = {
    njs_array_constructor_properties,
    njs_nitems(njs_array_constructor_properties),
};


static njs_int_t
njs_array_length(njs_vm_t *vm,njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    double        num;
    int64_t       size;
    uint32_t      length;
    njs_int_t     ret;
    njs_value_t   *val;
    njs_array_t   *array;
    njs_object_t  *proto;

    proto = njs_object(value);

    if (njs_fast_path(setval == NULL)) {
        array = njs_object_proto_lookup(proto, NJS_ARRAY, njs_array_t);
        if (njs_slow_path(array == NULL)) {
            njs_set_undefined(retval);
            return NJS_DECLINED;
        }

        njs_set_number(retval, array->length);
        return NJS_OK;
    }

    if (proto->type != NJS_ARRAY) {
        njs_set_undefined(retval);
        return NJS_DECLINED;
    }

    if (njs_slow_path(!njs_is_valid(setval))) {
        return NJS_DECLINED;
    }

    ret = njs_value_to_number(vm, setval, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = (uint32_t) njs_number_to_length(num);
    if ((double) length != num) {
        njs_range_error(vm, "Invalid array length");
        return NJS_ERROR;
    }

    array = (njs_array_t *) proto;

    if (njs_fast_path(array->object.fast_array)) {
        if (njs_fast_path(length <= NJS_ARRAY_LARGE_OBJECT_LENGTH)) {
            size = (int64_t) length - array->length;

            if (size > 0) {
                ret = njs_array_expand(vm, array, 0, size);
                if (njs_slow_path(ret != NJS_OK)) {
                    return NJS_ERROR;
                }

                val = &array->start[array->length];

                do {
                    njs_set_invalid(val);
                    val++;
                    size--;
                } while (size != 0);
            }

            array->length = length;

            *retval = *setval;
            return NJS_OK;
        }

        ret = njs_array_convert_to_slow_array(vm, array);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    prop->type = NJS_PROPERTY;
    njs_set_number(&prop->value, length);

    *retval = *setval;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_slice(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t      start, end, length, object_length;
    njs_int_t    ret;
    njs_value_t  *this;

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_object_length(vm, this, &object_length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    length = object_length;

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &start);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (start < 0) {
        start += length;

        if (start < 0) {
            start = 0;
        }
    }

    if (start >= length) {
        start = 0;
        length = 0;

    } else {
        if (njs_is_defined(njs_arg(args, nargs, 2))) {
            ret = njs_value_to_integer(vm, njs_argument(args, 2), &end);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

        } else {
            end = length;
        }

        if (end < 0) {
            end += length;
        }

        if (length >= end) {
            length = end - start;

            if (length < 0) {
                start = 0;
                length = 0;
            }

        } else {
            length -= start;
        }
    }

    return njs_array_prototype_slice_copy(vm, this, start, length);
}


static njs_int_t
njs_array_prototype_slice_copy(njs_vm_t *vm, njs_value_t *this,
    int64_t start, int64_t length)
{
    size_t             size;
    u_char             *dst;
    uint32_t           n;
    njs_int_t          ret;
    njs_array_t        *array, *keys;
    njs_value_t        *value, *last, retval, self;
    const u_char       *src, *end;
    njs_slice_prop_t   string_slice;
    njs_string_prop_t  string;

    keys = NULL;
    array = njs_array_alloc(vm, 0, length, NJS_ARRAY_SPARE);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(length == 0)) {
        ret = NJS_OK;
        goto done;
    }

    n = 0;

    if (njs_fast_path(array->object.fast_array)) {
        if (njs_is_string(this) || njs_is_object_string(this)) {

            if (njs_is_object_string(this)) {
                this = njs_object_value(this);
            }

            string_slice.start = start;
            string_slice.length = length;
            string_slice.string_length = njs_string_prop(&string, this);

            njs_string_slice_string_prop(&string, &string, &string_slice);

            src = string.start;
            end = src + string.size;

            if (string.length == 0) {
                /* Byte string. */
                do {
                    value = &array->start[n++];
                    dst = njs_string_short_start(value);
                    *dst = *src++;
                    njs_string_short_set(value, 1, 0);

                    length--;
                } while (length != 0);

            } else {
                /* UTF-8 or ASCII string. */
                do {
                    value = &array->start[n++];
                    dst = njs_string_short_start(value);
                    dst = njs_utf8_copy(dst, &src, end);
                    size = dst - njs_string_short_start(value);
                    njs_string_short_set(value, size, 1);

                    length--;
                } while (length != 0);
            }

        } else if (njs_is_object(this)) {

            last = &array->start[length];

            for (value = array->start; value < last; value++, start++) {
                ret = njs_value_property_i64(vm, this, start, value);
                if (njs_slow_path(ret != NJS_OK)) {
                    if (ret == NJS_ERROR) {
                        return NJS_ERROR;
                    }

                    njs_set_invalid(value);
                }
            }

        } else {

            /* Primitive types. */

            value = array->start;

            do {
                njs_set_invalid(value++);
                length--;
            } while (length != 0);
        }

        ret = NJS_OK;
        goto done;
    }

    njs_set_array(&self, array);

    if (njs_fast_object(length)) {
        do {
            ret = njs_value_property_i64(vm, this, start++, &retval);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }

            if (ret == NJS_OK) {
                ret = njs_value_property_i64_set(vm, &self, start, &retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return ret;
                }
            }

            length--;
        } while (length != 0);

        ret = NJS_OK;
        goto done;
    }

    keys = njs_array_indices(vm, this);
    if (njs_slow_path(keys == NULL)) {
        return NJS_ERROR;
    }

    for (n = 0; n < keys->length; n++) {
        ret = njs_value_property(vm, this, &keys->start[n], &retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto done;
        }

        ret = njs_value_property_set(vm, &self, &keys->start[n], &retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto done;
        }
    }

    ret = NJS_OK;

done:

    if (keys != NULL) {
        njs_array_destroy(vm, keys);
    }

    njs_set_array(&vm->retval, array);

    return ret;
}


static njs_int_t
njs_array_prototype_push(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t      length;
    njs_int_t    ret;
    njs_uint_t   i;
    njs_array_t  *array;
    njs_value_t  *this;

    length = 0;
    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_is_fast_array(this)) {
        array = njs_array(this);

        if (nargs != 0) {
            ret = njs_array_expand(vm, array, 0, nargs);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            for (i = 1; i < nargs; i++) {
                /* GC: njs_retain(&args[i]); */
                array->start[array->length++] = args[i];
            }
        }

        njs_set_number(&vm->retval, array->length);

        return NJS_OK;
    }

    ret = njs_object_length(vm, this, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_slow_path((length + nargs - 1) > NJS_MAX_LENGTH)) {
        njs_type_error(vm, "Invalid length");
        return NJS_ERROR;
    }

    for (i = 1; i < nargs; i++) {
        ret = njs_value_property_i64_set(vm, this, length++, &args[i]);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    ret = njs_object_length_set(vm, this, length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    njs_set_number(&vm->retval, length);

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_pop(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t      length;
    njs_int_t    ret;
    njs_value_t  *this;

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_object_length(vm, this, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (length == 0) {
        ret = njs_object_length_set(vm, this, length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        njs_set_undefined(&vm->retval);

        return NJS_OK;
    }

    ret = njs_value_property_i64(vm, this, --length, &vm->retval);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_is_fast_array(this)) {
        njs_array(this)->length--;

    } else {
        ret = njs_value_property_i64_delete(vm, this, length, NULL);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        ret = njs_object_length_set(vm, this, length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_unshift(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double       idx;
    int64_t      from, to, length;
    njs_int_t    ret;
    njs_uint_t   n;
    njs_array_t  *array, *keys;
    njs_value_t  *this, entry;

    this = njs_argument(args, 0);
    length = 0;
    n = nargs - 1;

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_fast_path(njs_is_fast_array(this))) {
        array = njs_array(this);

        if (n != 0) {
            ret = njs_array_expand(vm, array, n, 0);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            array->length += n;
            n = nargs;

            do {
                n--;
                /* GC: njs_retain(&args[n]); */
                array->start--;
                array->start[0] = args[n];
            } while (n > 1);
        }

        njs_set_number(&vm->retval, array->length);

        return NJS_OK;
    }

    ret = njs_object_length(vm, this, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (n == 0) {
        goto done;
    }

    if (njs_slow_path((length + n) > NJS_MAX_LENGTH)) {
        njs_type_error(vm, "Invalid length");
        return NJS_ERROR;
    }

    if (!njs_fast_object(length)) {
        keys = njs_array_indices(vm, this);
        if (njs_slow_path(keys == NULL)) {
            return NJS_ERROR;
        }

        from = keys->length;

        while (from > 0) {
            ret = njs_value_property_delete(vm, this, &keys->start[--from],
                                            &entry, 1);
            if (njs_slow_path(ret == NJS_ERROR)) {
                njs_array_destroy(vm, keys);
                return ret;
            }

            if (ret == NJS_OK) {
                idx = njs_string_to_index(&keys->start[from]) + n;

                ret = njs_value_property_i64_set(vm, this, idx, &entry);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    njs_array_destroy(vm, keys);
                    return ret;
                }
            }
        }

        njs_array_destroy(vm, keys);

        length += n;

        goto copy;
    }

    from = length;
    length += n;
    to = length;

    while (from > 0) {
        ret = njs_value_property_i64_delete(vm, this, --from, &entry);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        to--;

        if (ret == NJS_OK) {
            ret = njs_value_property_i64_set(vm, this, to, &entry);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }
        }
    }

copy:

    for (n = 1; n < nargs; n++) {
        ret = njs_value_property_i64_set(vm, this, n - 1, &args[n]);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

done:

    ret = njs_object_length_set(vm, this, length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    njs_set_number(&vm->retval, length);

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_shift(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t      i, length;
    njs_int_t    ret;
    njs_array_t  *array;
    njs_value_t  *this, entry;

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_object_length(vm, this, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (length == 0) {
        ret = njs_object_length_set(vm, this, length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        njs_set_undefined(&vm->retval);

        return NJS_OK;
    }

    ret = njs_value_property_i64(vm, this, 0, &vm->retval);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_is_fast_array(this)) {
        array = njs_array(this);

        array->start++;
        array->length--;

    } else {

        ret = njs_value_property_i64_delete(vm, this, 0, &vm->retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        for (i = 1; i < length; i++) {
            ret = njs_value_property_i64_delete(vm, this, i, &entry);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            if (ret == NJS_OK) {
                ret = njs_value_property_i64_set(vm, this, i - 1, &entry);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return ret;
                }
            }
        }

        ret = njs_object_length_set(vm, this, length - 1);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_splice(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t      i, n, start, length, items, delta, delete;
    njs_int_t    ret;
    njs_value_t  *this, value, del_object;
    njs_array_t  *array, *deleted;

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_object_length(vm, this, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &start);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    start = (start < 0) ? njs_max(length + start, 0) : njs_min(start, length);

    items = 0;
    delete = 0;

    if (nargs == 2) {
        delete = length - start;

    } else if (nargs > 2) {
        items = nargs - 3;

        ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &delete);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        delete = njs_min(njs_max(delete, 0), length - start);
    }

    delta = items - delete;

    if (njs_slow_path((length + delta) > NJS_MAX_LENGTH)) {
        njs_type_error(vm, "Invalid length");
        return NJS_ERROR;
    }

    /* TODO: ArraySpeciesCreate(). */

    deleted = njs_array_alloc(vm, 0, delete, 0);
    if (njs_slow_path(deleted == NULL)) {
        return NJS_ERROR;
    }

    if (njs_fast_path(njs_is_fast_array(this) && deleted->object.fast_array)) {
        array = njs_array(this);
        for (i = 0, n = start; i < delete; i++, n++) {
            deleted->start[i] = array->start[n];
        }

    } else {
        njs_set_array(&del_object, deleted);

        for (i = 0, n = start; i < delete; i++, n++) {
            ret = njs_value_property_i64(vm, this, n, &value);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }

            if (ret == NJS_OK) {
                /* TODO:  CreateDataPropertyOrThrow(). */
                ret = njs_value_property_i64_set(vm, &del_object, i, &value);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return ret;
                }

            } else {
                if (deleted->object.fast_array) {
                    njs_set_invalid(&deleted->start[i]);
                }
            }
        }

        ret = njs_object_length_set(vm, &del_object, delete);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (njs_fast_path(njs_is_fast_array(this))) {
        array = njs_array(this);

        if (delta != 0) {
            /*
             * Relocate the rest of items.
             * Index of the first item is in "n".
             */
            if (delta > 0) {
                ret = njs_array_expand(vm, array, 0, delta);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }
            }

            ret = njs_array_copy_within(vm, this, start + items, start + delete,
                                        array->length - (start + delete), 0);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            array->length += delta;
        }

        /* Copy new items. */

        if (items > 0) {
            memcpy(&array->start[start], &args[3],
                   items * sizeof(njs_value_t));
        }

    } else {

       if (delta != 0) {
           ret = njs_array_copy_within(vm, this, start + items, start + delete,
                                       length - (start + delete), delta < 0);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            for (i = length - 1; i >= length + delta; i--) {
                ret = njs_value_property_i64_delete(vm, this, i, NULL);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return NJS_ERROR;
                }
            }
       }

        /* Copy new items. */

        for (i = 3, n = start; items-- > 0; i++, n++) {
            ret = njs_value_property_i64_set(vm, this, n, &args[i]);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }
        }

        ret = njs_object_length_set(vm, this, length + delta);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    njs_set_array(&vm->retval, deleted);

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_reverse(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t      length, l, h;
    njs_int_t    ret, lret, hret;
    njs_value_t  value, lvalue, hvalue, *this;

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_object_length(vm, this, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_slow_path(length < 2)) {
        vm->retval = *this;
        return NJS_OK;
    }

    for (l = 0, h = length - 1; l < h; l++, h--) {
        lret = njs_value_property_i64(vm, this, l, &lvalue);
        if (njs_slow_path(lret == NJS_ERROR)) {
            return NJS_ERROR;
        }

        hret = njs_value_property_i64(vm, this, h, &hvalue);
        if (njs_slow_path(hret == NJS_ERROR)) {
            return NJS_ERROR;
        }

        if (lret == NJS_OK) {
            ret = njs_value_property_i64_set(vm, this, h, &lvalue);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }

            if (hret == NJS_OK) {
                ret = njs_value_property_i64_set(vm, this, l, &hvalue);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return NJS_ERROR;
                }

            } else {
                ret = njs_value_property_i64_delete(vm, this, l, &value);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return NJS_ERROR;
                }
            }

        } else if (hret == NJS_OK) {
            ret = njs_value_property_i64_set(vm, this, l, &hvalue);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }

            ret = njs_value_property_i64_delete(vm, this, h, &value);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }
        }
    }

    vm->retval = *this;

    return NJS_OK;
}


njs_int_t
njs_array_prototype_to_string(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t           ret;
    njs_value_t         value;
    njs_lvlhsh_query_t  lhq;

    static const njs_value_t  join_string = njs_string("join");

    if (njs_is_object(&args[0])) {
        njs_object_property_init(&lhq, &join_string, NJS_JOIN_HASH);

        ret = njs_object_property(vm, &args[0], &lhq, &value);

        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (njs_is_function(&value)) {
            return njs_function_apply(vm, njs_function(&value), args, nargs,
                                      &vm->retval);
        }
    }

    return njs_object_prototype_to_string(vm, args, nargs, unused);
}


static njs_int_t
njs_array_prototype_join(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char             *p, *last;
    int64_t            i, size, len, length;
    njs_int_t          ret;
    njs_chb_t          chain;
    njs_utf8_t         utf8;
    njs_value_t        *value, *this, entry;
    njs_string_prop_t  separator, string;

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    value = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_string(value))) {
        if (njs_is_undefined(value)) {
            value = njs_value_arg(&njs_string_comma);

        } else {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    (void) njs_string_prop(&separator, value);

    if (njs_slow_path(!njs_is_object(this))) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    length = 0;
    utf8 = njs_is_byte_string(&separator) ? NJS_STRING_BYTE : NJS_STRING_UTF8;

    ret = njs_object_length(vm, this, &len);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_slow_path(len == 0)) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    value = &entry;

    njs_chb_init(&chain, vm->mem_pool);

    for (i = 0; i < len; i++) {
        ret = njs_value_property_i64(vm, this, i, value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (!njs_is_null_or_undefined(value)) {
            if (!njs_is_string(value)) {
                last = njs_chb_current(&chain);

                ret = njs_value_to_chain(vm, &chain, value);
                if (njs_slow_path(ret < NJS_OK)) {
                    return ret;
                }

                if (last != njs_chb_current(&chain) && ret == 0) {
                    /*
                     * Appended values was a byte string.
                     */
                    utf8 = NJS_STRING_BYTE;
                }

                length += ret;

            } else {
                (void) njs_string_prop(&string, value);

                if (njs_is_byte_string(&string)) {
                    utf8 = NJS_STRING_BYTE;
                }

                length += string.length;
                njs_chb_append(&chain, string.start, string.size);
            }
        }

        length += separator.length;
        njs_chb_append(&chain, separator.start, separator.size);

        if (njs_slow_path(length > NJS_STRING_MAX_LENGTH)) {
            njs_range_error(vm, "invalid string length");
            return NJS_ERROR;
        }
    }

    njs_chb_drop(&chain, separator.size);

    size = njs_chb_size(&chain);
    if (njs_slow_path(size < 0)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    length -= separator.length;

    p = njs_string_alloc(vm, &vm->retval, size, utf8 ? length : 0);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    njs_chb_join_to(&chain, p);
    njs_chb_destroy(&chain);

    return NJS_OK;
}


static int
njs_array_indices_handler(const void *first, const void *second, void *ctx)
{
    double             num1, num2;
    int64_t            diff, cmp_res;
    njs_str_t          str1, str2;
    const njs_value_t  *val1, *val2;

    val1 = first;
    val2 = second;

    num1 = njs_string_to_index(val1);
    num2 = njs_string_to_index(val2);

    if (!isnan(num1) || !isnan(num2)) {
        if (isnan(num1)) {
            return 1;
        }

        if (isnan(num2)) {
            return -1;
        }

        diff = (int64_t) (num1 - num2);

        if (diff < 0) {
            return -1;
        }

        return diff != 0;
    }

    njs_string_get(val1, &str1);
    njs_string_get(val2, &str2);

    cmp_res =  strncmp((const char *) str1.start, (const char *) str2.start,
                   njs_min(str1.length, str2.length));
    if (cmp_res == 0) {
        if (str1.length < str2.length) {
            return -1;
        } else if (str1.length > str2.length) {
            return 1;
        } else {
            return 0;
        }
    }

    return cmp_res;
}


njs_array_t *
njs_array_keys(njs_vm_t *vm, njs_value_t *object, njs_bool_t all)
{
    njs_array_t  *keys;

    keys = njs_value_own_enumerate(vm, object, NJS_ENUM_KEYS, NJS_ENUM_STRING,
                                   all);
    if (njs_slow_path(keys == NULL)) {
        return NULL;
    }

    njs_qsort(keys->start, keys->length, sizeof(njs_value_t),
              njs_array_indices_handler, NULL);

    return keys;
}


njs_array_t *
njs_array_indices(njs_vm_t *vm, njs_value_t *object)
{
    double       idx;
    uint32_t     i;
    njs_array_t  *keys;

    keys = njs_array_keys(vm, object, 1);
    if (njs_slow_path(keys == NULL)) {
        return NULL;
    }

    for (i = 0; i < keys->length; i++) {
        idx = njs_string_to_index(&keys->start[i]);

        if (isnan(idx)) {
            keys->length = i;
            break;
        }
    }

    return keys;
}


njs_inline njs_int_t
njs_is_concat_spreadable(njs_vm_t *vm, njs_value_t *value)
{
    njs_int_t    ret;
    njs_value_t  retval;

    static const njs_value_t  key =
                         njs_wellknown_symbol(NJS_SYMBOL_IS_CONCAT_SPREADABLE);

    if (njs_slow_path(!njs_is_object(value))) {
        return NJS_DECLINED;
    }

    ret = njs_value_property(vm, value, njs_value_arg(&key), &retval);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_is_defined(&retval)) {
        return njs_bool(&retval) ? NJS_OK : NJS_DECLINED;
    }

    return njs_is_array(value) ? NJS_OK : NJS_DECLINED;
}


static njs_int_t
njs_array_prototype_concat(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double       idx;
    int64_t      k, len, length;
    njs_int_t    ret;
    njs_uint_t   i;
    njs_value_t  this, retval, *e;
    njs_array_t  *array, *keys;

    ret = njs_value_to_object(vm, &args[0]);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* TODO: ArraySpeciesCreate(). */

    array = njs_array_alloc(vm, 0, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    njs_set_array(&this, array);

    len = 0;
    length = 0;

    for (i = 0; i < nargs; i++) {
        e = njs_argument(args, i);

        ret = njs_is_concat_spreadable(vm, e);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return NJS_ERROR;
        }

        if (ret == NJS_OK) {
            ret = njs_object_length(vm, e, &len);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            if (njs_slow_path((length + len) > NJS_MAX_LENGTH)) {
                njs_type_error(vm, "Invalid length");
                return NJS_ERROR;
            }

            if (njs_is_fast_array(e) || njs_fast_object(len)) {
                for (k = 0; k < len; k++, length++) {
                    ret = njs_value_property_i64(vm, e, k, &retval);
                    if (njs_slow_path(ret != NJS_OK)) {
                        if (ret == NJS_ERROR) {
                            return NJS_ERROR;
                        }

                        njs_set_invalid(&retval);
                    }

                    ret = njs_value_property_i64_set(vm, &this, length,
                                                     &retval);
                    if (njs_slow_path(ret == NJS_ERROR)) {
                        return ret;
                    }
                }

                continue;
            }

            keys = njs_array_indices(vm, e);
            if (njs_slow_path(keys == NULL)) {
                return NJS_ERROR;
            }

            for (k = 0; k < keys->length; k++) {
                ret = njs_value_property(vm, e, &keys->start[k], &retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return ret;
                }

                if (ret == NJS_OK) {
                    idx = njs_string_to_index(&keys->start[k]) + length;

                    ret = njs_value_property_i64_set(vm, &this, idx, &retval);
                    if (njs_slow_path(ret == NJS_ERROR)) {
                        njs_array_destroy(vm, keys);
                        return ret;
                    }
                }
            }

            njs_array_destroy(vm, keys);

            length += len;

            continue;
        }

        if (njs_slow_path((length + len) >= NJS_MAX_LENGTH)) {
            njs_type_error(vm, "Invalid length");
            return NJS_ERROR;
        }

        ret = njs_value_property_i64_set(vm, &this, length, e);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        length++;
    }

    ret = njs_object_length_set(vm, &this, length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    vm->retval = this;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_fill(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t       i, length, start, end;
    njs_int_t     ret;
    njs_array_t   *array;
    njs_value_t   *this, *value;

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    array = NULL;

    if (njs_is_fast_array(this)) {
        array = njs_array(this);
        length = array->length;

    } else {
        ret = njs_object_length(vm, this, &length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &start);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    start = (start < 0) ? njs_max(length + start, 0) : njs_min(start, length);

    if (njs_is_undefined(njs_arg(args, nargs, 3))) {
        end = length;

    } else {
        ret = njs_value_to_integer(vm, njs_arg(args, nargs, 3), &end);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    end = (end < 0) ? njs_max(length + end, 0) : njs_min(end, length);

    value = njs_arg(args, nargs, 1);

    if (array != NULL) {
        for (i = start; i < end; i++) {
            array->start[i] = *value;
        }

        vm->retval = *this;

        return NJS_OK;
    }

    value = njs_arg(args, nargs, 1);

    while (start < end) {
        ret = njs_value_property_i64_set(vm, this, start++, value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    vm->retval = *this;

    return NJS_OK;
}


njs_inline njs_int_t
njs_array_iterator_call(njs_vm_t *vm, njs_iterator_args_t *args,
    const njs_value_t *entry, uint32_t n)
{
    njs_value_t  arguments[3];

    /* GC: array elt, array */

    arguments[0] = *entry;
    njs_set_number(&arguments[1], n);
    arguments[2] = *args->value;

    return njs_function_call(vm, args->function, args->argument, arguments, 3,
                             &vm->retval);
}


static njs_int_t
njs_array_handler_every(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    njs_int_t  ret;

    if (njs_is_valid(entry)) {
        ret = njs_array_iterator_call(vm, args, entry, n);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (!njs_is_true(&vm->retval)) {
            vm->retval = njs_value_false;
            return NJS_DONE;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_some(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    njs_int_t  ret;

    if (njs_is_valid(entry)) {
        ret = njs_array_iterator_call(vm, args, entry, n);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_true(&vm->retval)) {
            vm->retval = njs_value_true;
            return NJS_DONE;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_includes(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    if (!njs_is_valid(entry)) {
        entry = njs_value_arg(&njs_value_undefined);
    }

    if (njs_values_same_zero(args->argument, entry)) {
        njs_set_true(&vm->retval);

        return NJS_DONE;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_index_of(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    if (njs_values_strict_equal(args->argument, entry)) {
        njs_set_number(&vm->retval, n);

        return NJS_DONE;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_for_each(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    if (njs_is_valid(entry)) {
        return njs_array_iterator_call(vm, args, entry, n);
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_find(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    njs_int_t    ret;
    njs_value_t  copy;

    if (njs_is_valid(entry)) {
        copy = *entry;

    } else {
        njs_set_undefined(&copy);
    }

    ret = njs_array_iterator_call(vm, args, &copy, n);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_is_true(&vm->retval)) {
        vm->retval = copy;

        return NJS_DONE;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_find_index(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    njs_int_t    ret;
    njs_value_t  copy;

    if (njs_is_valid(entry)) {
        copy = *entry;

    } else {
        njs_set_undefined(&copy);
    }

    ret = njs_array_iterator_call(vm, args, &copy, n);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_is_true(&vm->retval)) {
        njs_set_number(&vm->retval, n);

        return NJS_DONE;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_reduce(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    njs_int_t    ret;
    njs_value_t  arguments[5];

    if (njs_is_valid(entry)) {
        if (!njs_is_valid(args->argument)) {
            *(args->argument) = *entry;
            return NJS_OK;
        }

        /* GC: array elt, array */

        njs_set_undefined(&arguments[0]);
        arguments[1] = *args->argument;
        arguments[2] = *entry;
        njs_set_number(&arguments[3], n);
        arguments[4] = *args->value;

        ret =  njs_function_apply(vm, args->function, arguments, 5,
                                  args->argument);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_filter(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    njs_int_t    ret;
    njs_value_t  copy;

    if (njs_is_valid(entry)) {
        copy = *entry;

        ret = njs_array_iterator_call(vm, args, &copy, n);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_true(&vm->retval)) {
            ret = njs_array_add(vm, args->data, &copy);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_map(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *entry, int64_t n)
{
    njs_int_t    ret;
    njs_array_t  *retval;
    njs_value_t  this;

    retval = args->data;

    if (retval->object.fast_array) {
        njs_set_invalid(&retval->start[n]);
    }

    if (njs_is_valid(entry)) {
        ret = njs_array_iterator_call(vm, args, entry, n);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_valid(&vm->retval)) {
            if (retval->object.fast_array) {
                retval->start[n] = vm->retval;

            } else {
                njs_set_array(&this, retval);

                ret = njs_value_property_i64_set(vm, &this, n, &vm->retval);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }
            }
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_iterator(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
{
    int64_t                 i, length;
    njs_int_t               ret;
    njs_array_t             *array;
    njs_value_t             accumulator;
    njs_iterator_args_t     iargs;
    njs_iterator_handler_t  handler;

    iargs.value = njs_argument(args, 0);

    ret = njs_value_to_object(vm, iargs.value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_length(vm, iargs.value, &iargs.to);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    iargs.from = 0;

    if (njs_array_arg1(magic) == NJS_ARRAY_FUNC) {
        if (njs_slow_path(!njs_is_function(njs_arg(args, nargs, 1)))) {
            njs_type_error(vm, "callback argument is not callable");
            return NJS_ERROR;
        }

        iargs.function = njs_function(njs_argument(args, 1));
        iargs.argument = njs_arg(args, nargs, 2);

    } else {
        iargs.argument = njs_arg(args, nargs, 1);
    }

    switch (njs_array_type(magic)) {
    case NJS_ARRAY_EVERY:
        handler = njs_array_handler_every;
        break;

    case NJS_ARRAY_SOME:
        handler = njs_array_handler_some;
        break;

    case NJS_ARRAY_INCLUDES:
    case NJS_ARRAY_INDEX_OF:
        switch (njs_array_type(magic)) {
        case NJS_ARRAY_INCLUDES:
            handler = njs_array_handler_includes;

            if (iargs.to == 0) {
                goto done;
            }

            break;

        default:
            handler = njs_array_handler_index_of;
        }

        ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &iargs.from);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (iargs.from < 0) {
            iargs.from += iargs.to;

            if (iargs.from < 0) {
                iargs.from = 0;
            }
        }

        break;

    case NJS_ARRAY_FOR_EACH:
        handler = njs_array_handler_for_each;
        break;

    case NJS_ARRAY_FIND:
        handler = njs_array_handler_find;
        break;

    case NJS_ARRAY_FIND_INDEX:
        handler = njs_array_handler_find_index;
        break;

    case NJS_ARRAY_REDUCE:
        handler = njs_array_handler_reduce;

        njs_set_invalid(&accumulator);

        if (nargs > 2) {
            accumulator = *iargs.argument;
        }

        iargs.argument = &accumulator;
        break;

    case NJS_ARRAY_FILTER:
    case NJS_ARRAY_MAP:
    default:
        if (njs_array_type(magic) == NJS_ARRAY_FILTER) {
            length = 0;
            handler = njs_array_handler_filter;

        } else {
            length = iargs.to;
            handler = njs_array_handler_map;
        }

        array = njs_array_alloc(vm, 0, length, NJS_ARRAY_SPARE);
        if (njs_slow_path(array == NULL)) {
            return NJS_ERROR;
        }

        if (array->object.fast_array) {
            for (i = 0; i < length; i++) {
                njs_set_invalid(&array->start[i]);
            }
        }

        iargs.data = array;

        break;
    }

    ret = njs_object_iterate(vm, &iargs, handler);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_DONE) {
        return NJS_OK;
    }

done:

    /* Default values. */

    switch (njs_array_type(magic)) {
    case NJS_ARRAY_EVERY:
        vm->retval = njs_value_true;
        break;

    case NJS_ARRAY_SOME:
    case NJS_ARRAY_INCLUDES:
        vm->retval = njs_value_false;
        break;

    case NJS_ARRAY_INDEX_OF:
    case NJS_ARRAY_FIND_INDEX:
        njs_set_number(&vm->retval, -1);
        break;

    case NJS_ARRAY_FOR_EACH:
    case NJS_ARRAY_FIND:
        njs_set_undefined(&vm->retval);
        break;

    case NJS_ARRAY_REDUCE:
        if (!njs_is_valid(&accumulator)) {
            njs_type_error(vm, "Reduce of empty object with no initial value");
            return NJS_ERROR;
        }

        vm->retval = accumulator;
        break;

    case NJS_ARRAY_FILTER:
    case NJS_ARRAY_MAP:
    default:
        njs_set_array(&vm->retval, iargs.data);
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_reverse_iterator(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t type)
{
    int64_t                 from, length;
    njs_int_t               ret;
    njs_value_t             accumulator;
    njs_iterator_args_t     iargs;
    njs_iterator_handler_t  handler;

    iargs.value = njs_argument(args, 0);

    ret = njs_value_to_object(vm, iargs.value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    iargs.argument = njs_arg(args, nargs, 1);

    ret = njs_value_length(vm, iargs.value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    switch (type) {
    case NJS_ARRAY_LAST_INDEX_OF:
        handler = njs_array_handler_index_of;

        if (length == 0) {
            goto done;
        }

        if (nargs > 2) {
            ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &from);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

        } else {
            from = length - 1;
        }

        if (from >= 0) {
            from = njs_min(from, length - 1);

        } else if (from < 0) {
            from += length;
        }

        break;

    case NJS_ARRAY_REDUCE_RIGHT:
    default:
        handler = njs_array_handler_reduce;

        if (njs_slow_path(!njs_is_function(njs_arg(args, nargs, 1)))) {
            njs_type_error(vm, "callback argument is not callable");
            return NJS_ERROR;
        }

        njs_set_invalid(&accumulator);

        iargs.function = njs_function(njs_argument(args, 1));
        iargs.argument = &accumulator;

        if (nargs > 2) {
            accumulator = *njs_argument(args, 2);

        } else if (length == 0) {
            goto done;
        }

        from = length - 1;
        break;
    }

    iargs.from = from;
    iargs.to = 0;

    ret = njs_object_iterate_reverse(vm, &iargs, handler);
    if (njs_fast_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (ret == NJS_DONE) {
        return NJS_OK;
    }

done:

    switch (type) {
    case NJS_ARRAY_LAST_INDEX_OF:
        njs_set_number(&vm->retval, -1);
        break;

    case NJS_ARRAY_REDUCE_RIGHT:
    default:
        if (!njs_is_valid(&accumulator)) {
            njs_type_error(vm, "Reduce of empty object with no initial value");
            return NJS_ERROR;
        }

        vm->retval = accumulator;
        break;
    }

    return NJS_OK;
}


typedef struct {
    njs_value_t            value;
    njs_value_t            *str;
    int64_t                pos;
} njs_array_sort_slot_t;


typedef struct {
    njs_vm_t               *vm;
    njs_function_t         *function;
    njs_bool_t             exception;

    njs_arr_t              strings;
} njs_array_sort_ctx_t;


static int
njs_array_compare(const void *a, const void *b, void *c)
{
    double                 num;
    njs_int_t              ret;
    njs_value_t            arguments[3], retval;
    njs_array_sort_ctx_t   *ctx;
    njs_array_sort_slot_t  *aslot, *bslot;

    ctx = c;

    if (ctx->exception) {
        return 0;
    }

    aslot = (njs_array_sort_slot_t *) a;
    bslot = (njs_array_sort_slot_t *) b;

    if (ctx->function != NULL) {
        njs_set_undefined(&arguments[0]);
        arguments[1] = aslot->value;
        arguments[2] = bslot->value;

        ret = njs_function_apply(ctx->vm, ctx->function, arguments, 3, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        ret = njs_value_to_number(ctx->vm, &retval, &num);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        if (njs_slow_path(isnan(num))) {
            return 0;
        }

        if (num != 0) {
            return (num > 0) - (num < 0);
        }

        goto compare_same;
    }

    if (aslot->str == NULL) {
        aslot->str = njs_arr_add(&ctx->strings);
        ret = njs_value_to_string(ctx->vm, aslot->str, &aslot->value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }
    }

    if (bslot->str == NULL) {
        bslot->str = njs_arr_add(&ctx->strings);
        ret = njs_value_to_string(ctx->vm, bslot->str, &bslot->value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }
    }

    ret = njs_string_cmp(aslot->str, bslot->str);

    if (ret != 0) {
        return ret;
    }

compare_same:

    /* Ensures stable sorting. */

    return (aslot->pos > bslot->pos) - (aslot->pos < bslot->pos);

exception:

    ctx->exception = 1;

    return 0;
}


static njs_int_t
njs_array_prototype_sort(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t                i, und, len, nlen, length;
    njs_int_t              ret, fast_path;
    njs_array_t            *array;
    njs_value_t            *this, *comparefn, *start, *strings;
    njs_array_sort_ctx_t   ctx;
    njs_array_sort_slot_t  *p, *end, *slots, *nslots;

    comparefn = njs_arg(args, nargs, 1);

    if (njs_is_defined(comparefn)) {
        if (njs_slow_path(!njs_is_function(comparefn))) {
            njs_type_error(vm, "comparefn must be callable or undefined");
            return NJS_ERROR;
        }

        ctx.function = njs_function(comparefn);

    } else {
        ctx.function = NULL;
    }

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_length(vm, this, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(length < 2)) {
        vm->retval = *this;
        return NJS_OK;
    }

    slots = NULL;
    ctx.vm = vm;
    ctx.strings.separate = 0;
    ctx.strings.pointer = 0;
    ctx.exception = 0;

    fast_path = njs_is_fast_array(this);

    if (njs_fast_path(fast_path)) {
        array = njs_array(this);
        start = array->start;

        slots = njs_mp_alloc(vm->mem_pool,
                             sizeof(njs_array_sort_slot_t) * length);
        if (njs_slow_path(slots == NULL)) {
                return NJS_ERROR;
        }

        und = 0;
        p = slots;

        for (i = 0; i < length; i++) {
            if (njs_slow_path(!njs_is_valid(&start[i]))) {
                fast_path = 0;
                njs_mp_free(vm->mem_pool, slots);
                slots = NULL;
                goto slow_path;
            }

            if (njs_slow_path(njs_is_undefined(&start[i]))) {
                und++;
                continue;
            }

            p->value = start[i];
            p->pos = i;
            p->str = NULL;
            p++;
        }

        len = p - slots;

    } else {

slow_path:

        und = 0;
        p = NULL;
        end = NULL;

        for (i = 0; i < length; i++) {
            if (p >= end) {
                nlen = njs_min(njs_max((p - slots) * 2, 8), length);
                nslots = njs_mp_alloc(vm->mem_pool,
                                      sizeof(njs_array_sort_slot_t) * nlen);
                if (njs_slow_path(nslots == NULL)) {
                    njs_memory_error(vm);
                    return NJS_ERROR;
                }

                if (slots != NULL) {
                    p = (void *) njs_cpymem(nslots, slots,
                                  sizeof(njs_array_sort_slot_t) * (p - slots));
                    njs_mp_free(vm->mem_pool, slots);

                } else {
                    p = nslots;
                }

                slots = nslots;
                end = slots + nlen;
            }

            ret = njs_value_property_i64(vm, this, i, &p->value);
            if (njs_slow_path(ret == NJS_ERROR)) {
                ret = NJS_ERROR;
                goto exception;
            }

            if (ret == NJS_DECLINED) {
                continue;
            }

            if (njs_is_undefined(&p->value)) {
                und++;
                continue;
            }

            p->pos = i;
            p->str = NULL;
            p++;
        }

        len = p - slots;
    }

    strings = njs_arr_init(vm->mem_pool, &ctx.strings, NULL, len + 1,
                           sizeof(njs_value_t));
    if (njs_slow_path(strings == NULL)) {
        ret = NJS_ERROR;
        goto exception;
    }

    njs_qsort(slots, len, sizeof(njs_array_sort_slot_t), njs_array_compare,
              &ctx);

    if (ctx.exception) {
        ret = NJS_ERROR;
        goto exception;
    }

    if (njs_fast_path(fast_path && njs_is_fast_array(this))) {
        array = njs_array(this);
        start = array->start;

        for (i = 0; i < len; i++) {
            start[i] = slots[i].value;
        }

        for (i = len; und-- > 0; i++) {
            start[i] = njs_value_undefined;
        }

    } else {
        for (i = 0; i < len; i++) {
            if (slots[i].pos != i) {
                ret = njs_value_property_i64_set(vm, this, i, &slots[i].value);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto exception;
                }
            }
        }

        for (i = len; und-- > 0; i++) {
            ret = njs_value_property_i64_set(vm, this, i,
                                          njs_value_arg(&njs_value_undefined));
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto exception;
            }
        }

        for (; i < length; i++) {
            ret = njs_value_property_i64_delete(vm, this, i, NULL);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto exception;
            }
        }
    }

    vm->retval = *this;

    ret = NJS_OK;

exception:

    if (slots != NULL) {
        njs_mp_free(vm->mem_pool, slots);
    }

    njs_arr_destroy(&ctx.strings);

    return ret;
}


static njs_int_t
njs_array_prototype_copy_within(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    int64_t      length, count, to, from, end;
    njs_int_t    ret;
    njs_value_t  *this, *value;

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_length(vm, this, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &to);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    to = (to < 0) ? njs_max(length + to, 0) : njs_min(to, length);

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &from);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    from = (from < 0) ? njs_max(length + from, 0) : njs_min(from, length);

    value = njs_arg(args, nargs, 3);

    if (njs_is_undefined(value)) {
        end = length;

    } else {
        ret = njs_value_to_integer(vm, value, &end);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    end = (end < 0) ? njs_max(length + end, 0) : njs_min(end, length);

    count = njs_min(end - from, length - to);

    njs_vm_retval_set(vm, this);

    return njs_array_copy_within(vm, this, to, from, count,
                                 !(from < to && to < from + count));
}


static njs_int_t
njs_array_prototype_iterator_obj(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t kind)
{
    njs_int_t    ret;
    njs_value_t  *this;

    this = njs_argument(args, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_array_iterator_create(vm, this, &vm->retval, kind);
}


static const njs_object_prop_t  njs_array_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("length"),
        .value = njs_prop_handler(njs_array_length),
        .writable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("concat"),
        .value = njs_native_function(njs_array_prototype_concat, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("copyWithin"),
        .value = njs_native_function(njs_array_prototype_copy_within, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("entries"),
        .value = njs_native_function2(njs_array_prototype_iterator_obj, 0,
                                      NJS_ENUM_BOTH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("every"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_func(NJS_ARRAY_EVERY)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("fill"),
        .value = njs_native_function(njs_array_prototype_fill, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("filter"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_func(NJS_ARRAY_FILTER)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("find"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_func(NJS_ARRAY_FIND)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("findIndex"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_func(NJS_ARRAY_FIND_INDEX)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("forEach"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_func(NJS_ARRAY_FOR_EACH)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("includes"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_arg(NJS_ARRAY_INCLUDES)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("indexOf"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_arg(NJS_ARRAY_INDEX_OF)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("join"),
        .value = njs_native_function(njs_array_prototype_join, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("keys"),
        .value = njs_native_function2(njs_array_prototype_iterator_obj, 0,
                                      NJS_ENUM_KEYS),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("lastIndexOf"),
        .value = njs_native_function2(njs_array_prototype_reverse_iterator, 1,
                                      NJS_ARRAY_LAST_INDEX_OF),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("map"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_func(NJS_ARRAY_MAP)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("pop"),
        .value = njs_native_function(njs_array_prototype_pop, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("push"),
        .value = njs_native_function(njs_array_prototype_push, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("reduce"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_func(NJS_ARRAY_REDUCE)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("reduceRight"),
        .value = njs_native_function2(njs_array_prototype_reverse_iterator, 1,
                                      NJS_ARRAY_REDUCE_RIGHT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("reverse"),
        .value = njs_native_function(njs_array_prototype_reverse, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("shift"),
        .value = njs_native_function(njs_array_prototype_shift, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("slice"),
        .value = njs_native_function(njs_array_prototype_slice, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("some"),
        .value = njs_native_function2(njs_array_prototype_iterator, 1,
                                      njs_array_func(NJS_ARRAY_SOME)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sort"),
        .value = njs_native_function(njs_array_prototype_sort, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("splice"),
        .value = njs_native_function(njs_array_prototype_splice, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_array_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("unshift"),
        .value = njs_native_function(njs_array_prototype_unshift, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("values"),
        .value = njs_native_function2(njs_array_prototype_iterator_obj, 0,
                                      NJS_ENUM_VALUES),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_ITERATOR),
        .value = njs_native_function2(njs_array_prototype_iterator_obj, 0,
                                      NJS_ENUM_VALUES),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_array_prototype_init = {
    njs_array_prototype_properties,
    njs_nitems(njs_array_prototype_properties),
};


const njs_object_prop_t  njs_array_instance_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("length"),
        .value = njs_prop_handler(njs_array_length),
        .writable = 1
    },
};


const njs_object_init_t  njs_array_instance_init = {
    njs_array_instance_properties,
    njs_nitems(njs_array_instance_properties),
};


const njs_object_type_init_t  njs_array_type_init = {
    .constructor = njs_native_ctor(njs_array_constructor, 1, 0),
    .constructor_props = &njs_array_constructor_init,
    .prototype_props = &njs_array_prototype_init,
    .prototype_value = { .object = { .type = NJS_ARRAY, .fast_array = 1 } },
};
