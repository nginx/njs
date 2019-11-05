
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


#define NJS_ARRAY_LARGE_OBJECT_LENGTH  4096


typedef struct {
    njs_function_t  *function;
    njs_value_t     *argument;
    njs_value_t     *value;

    njs_array_t     *array;

    uint32_t        from;
    uint32_t        to;
} njs_array_iterator_args_t;


typedef njs_int_t (*njs_array_iterator_handler_t)(njs_vm_t *vm,
    njs_array_iterator_args_t *args, njs_value_t *entry, uint32_t n);


static njs_int_t njs_array_prototype_slice_copy(njs_vm_t *vm,
    njs_value_t *this, int64_t start, int64_t length);
static njs_value_t *njs_array_copy(njs_value_t *dst, njs_value_t *src);
static njs_array_t *njs_object_indexes(njs_vm_t *vm, njs_value_t *object);


njs_array_t *
njs_array_alloc(njs_vm_t *vm, uint64_t length, uint32_t spare)
{
    uint64_t     size;
    njs_array_t  *array;

    if (njs_slow_path(length > UINT32_MAX)) {
        goto overflow;
    }

    size = length + spare;

    if (njs_slow_path(size > NJS_ARRAY_MAX_LENGTH)) {
        goto memory_error;
    }

    array = njs_mp_alloc(vm->mem_pool, sizeof(njs_array_t));
    if (njs_slow_path(array == NULL)) {
        goto memory_error;
    }

    array->data = njs_mp_align(vm->mem_pool, sizeof(njs_value_t),
                               size * sizeof(njs_value_t));
    if (njs_slow_path(array->data == NULL)) {
        goto memory_error;
    }

    array->start = array->data;
    njs_lvlhsh_init(&array->object.hash);
    array->object.shared_hash = vm->shared->array_instance_hash;
    array->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_ARRAY].object;
    array->object.type = NJS_ARRAY;
    array->object.shared = 0;
    array->object.extensible = 1;
    array->size = size;
    array->length = length;

    return array;

memory_error:

    njs_memory_error(vm);

    return NULL;

overflow:

    njs_range_error(vm, "Invalid array length");

    return NULL;
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

    if (njs_slow_path(size > NJS_ARRAY_MAX_LENGTH)) {
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

    njs_mp_free(vm->mem_pool, old);

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
        size = (uint32_t) num;

        if ((double) size != num) {
            njs_range_error(vm, "Invalid array length");
            return NJS_ERROR;
        }

        args = NULL;
    }

    array = njs_array_alloc(vm, size, NJS_ARRAY_SPARE);

    if (njs_fast_path(array != NULL)) {

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
njs_array_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
        uint32_t     length, i;
        njs_array_t  *array;

        length = nargs > 1 ? nargs - 1 : 0;

        array = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
        if (njs_slow_path(array == NULL)) {
            return NJS_ERROR;
        }

        njs_set_array(&vm->retval, array);

        for (i = 0; i < length; i++) {
            array->start[i] = args[i + 1];
        }

        return NJS_OK;
}


static const njs_object_prop_t  njs_array_constructor_properties[] =
{
    /* Array.name == "Array". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Array"),
        .configurable = 1,
    },

    /* Array.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* Array.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    /* Array.isArray(). */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("isArray"),
        .value = njs_native_function(njs_array_is_array, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    /* Array.of(). */
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
        do {
            if (njs_fast_path(proto->type == NJS_ARRAY)) {
                break;
            }

            proto = proto->__proto__;
        } while (proto != NULL);

        if (njs_slow_path(proto == NULL)) {
            njs_set_undefined(retval);
            return NJS_DECLINED;
        }

        array = (njs_array_t *) proto;

        njs_set_number(retval, array->length);
        return NJS_OK;
    }

    if (proto->type != NJS_ARRAY) {
        njs_set_undefined(retval);
        return NJS_DECLINED;
    }

    if (njs_slow_path(!njs_is_number(setval))) {
        ret = njs_value_to_number(vm, setval, &num);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {
        num = njs_number(setval);
    }

    length = njs_number_to_length(num);

    if ((double) length != num) {
        njs_range_error(vm, "Invalid array length");
        return NJS_ERROR;
    }

    array = (njs_array_t *) proto;

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


/*
 * Array.slice(start[, end]).
 * JavaScript 1.2, ECMAScript 3.
 */

static njs_int_t
njs_array_prototype_slice(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t      start, end, length;
    uint32_t     object_length;
    njs_int_t    ret;
    njs_value_t  *value;

    value = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_object_length(vm, value, &object_length);
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

    return njs_array_prototype_slice_copy(vm, value, start, length);
}


static njs_int_t
njs_array_prototype_slice_copy(njs_vm_t *vm, njs_value_t *this,
    int64_t start, int64_t length)
{
    size_t             size;
    u_char             *dst;
    uint32_t           n;
    njs_int_t          ret;
    njs_array_t        *array;
    njs_value_t        *value, name;
    const u_char       *src, *end;
    njs_slice_prop_t   string_slice;
    njs_string_prop_t  string;

    array = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    njs_set_array(&vm->retval, array);

    if (length != 0) {
        n = 0;

        if (njs_fast_path(njs_is_array(this))) {
            value = njs_array_start(this);

            do {
                /* GC: retain long string and object in values[start]. */
                array->start[n++] = value[start++];
                length--;
            } while (length != 0);

        } else if (njs_is_string(this) || this->type == NJS_OBJECT_STRING) {

            if (this->type == NJS_OBJECT_STRING) {
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

            do {
                njs_uint32_to_string(&name, start++);

                value = &array->start[n++];
                ret = njs_value_property(vm, this, &name, value);

                if (ret != NJS_OK) {
                    njs_set_invalid(value);
                }

                length--;
            } while (length != 0);

        } else {

            /* Primitive types. */

            value = array->start;

            do {
                njs_set_invalid(value++);
                length--;
            } while (length != 0);
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_push(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t     length;
    njs_int_t    ret;
    njs_uint_t   i;
    njs_array_t  *array;
    njs_value_t  *value, index;

    value = njs_arg(args, nargs, 0);
    length = 0;

    ret = njs_value_to_object(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_is_array(&args[0])) {
        array = njs_array(&args[0]);

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

    ret = njs_object_length(vm, value, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    for (i = 1; i < nargs; i++) {
        njs_uint32_to_string(&index, length++);

        ret = njs_value_property_set(vm, value, &index, &args[i]);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    ret = njs_object_length_set(vm, value, length);
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
    uint32_t     length;
    njs_int_t    ret;
    njs_array_t  *array;
    njs_value_t  *value, *entry, index;

    value = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_undefined(&vm->retval);

    if (njs_is_array(&args[0])) {
        array = njs_array(&args[0]);

        if (array->length != 0) {
            array->length--;
            entry = &array->start[array->length];

            if (njs_is_valid(entry)) {
                vm->retval = *entry;
            }
        }

        return NJS_OK;
    }

    ret = njs_object_length(vm, value, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (length != 0) {
        njs_uint32_to_string(&index, --length);

        ret = njs_value_property_delete(vm, value, &index, &vm->retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    ret = njs_object_length_set(vm, value, length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_unshift(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double       idx;
    uint32_t     from, to, length;
    njs_int_t    ret;
    njs_uint_t   n;
    njs_array_t  *array, *keys;
    njs_value_t  *value, entry, index;

    value = njs_arg(args, nargs, 0);
    length = 0;
    n = nargs - 1;

    ret = njs_value_to_object(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_is_array(value)) {
        array = njs_array(value);

        if (array->length > (UINT32_MAX - n)) {
            njs_type_error(vm, "Invalid length");
            return NJS_ERROR;
        }

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

    ret = njs_object_length(vm, value, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (n == 0) {
        goto done;
    }

    if (length > (UINT32_MAX - n)) {
        njs_type_error(vm, "Invalid length");
        return NJS_ERROR;
    }

    if (length > NJS_ARRAY_LARGE_OBJECT_LENGTH) {
        keys = njs_object_indexes(vm, value);
        if (njs_slow_path(keys == NULL)) {
            return NJS_ERROR;
        }

        from = keys->length;

        while (from > 0) {
            ret = njs_value_property_delete(vm, value, &keys->start[--from],
                                            &entry);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            if (ret == NJS_OK) {
                idx = njs_string_to_index(&keys->start[from]);

                njs_uint32_to_string(&index, (uint32_t) idx + nargs - 1);

                ret = njs_value_property_set(vm, value, &index, &entry);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return ret;
                }
            }
        }

        length += nargs - 1;

        goto copy;
    }

    from = length;
    length += n;
    to = length;

    while (from > 0) {
        njs_uint32_to_string(&index, --from);

        ret = njs_value_property_delete(vm, value, &index, &entry);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        to--;

        if (ret == NJS_OK) {
            njs_uint32_to_string(&index, to);

            ret = njs_value_property_set(vm, value, &index, &entry);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }
        }
    }

copy:

    for (n = 1; n < nargs; n++) {
        njs_uint32_to_string(&index, n - 1);

        ret = njs_value_property_set(vm, value, &index, &args[n]);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

done:

    ret = njs_object_length_set(vm, value, length);
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
    uint32_t     i, length;
    njs_int_t    ret;
    njs_array_t  *array;
    njs_value_t  *value, *item, entry, index;

    value = njs_arg(args, nargs, 0);
    length = 0;

    ret = njs_value_to_object(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_undefined(&vm->retval);

    if (njs_is_array(&args[0])) {
        array = njs_array(&args[0]);

        if (array->length != 0) {
            array->length--;

            item = &array->start[0];
            array->start++;

            if (njs_is_valid(item)) {
                vm->retval = *item;
            }
        }

        return NJS_OK;
    }

    ret = njs_object_length(vm, value, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (length == 0) {
        goto done;
    }

    njs_uint32_to_string(&index, 0);

    ret = njs_value_property_delete(vm, value, &index, &vm->retval);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    for (i = 1; i < length; i++) {
        njs_uint32_to_string(&index, i);

        ret = njs_value_property_delete(vm, value, &index, &entry);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (ret == NJS_OK) {
            njs_uint32_to_string(&index, i - 1);

            ret = njs_value_property_set(vm, value, &index, &entry);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }
        }
    }

    length--;

done:

    ret = njs_object_length_set(vm, value, length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_splice(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t      n, start, length, items, delta, delete;
    njs_int_t    ret;
    njs_uint_t   i;
    njs_value_t  *value;
    njs_array_t  *array, *deleted;

    value = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    array = NULL;
    start = 0;
    delete = 0;

    if (njs_is_array(value)) {
        array = njs_array(value);
        length = array->length;

        if (nargs > 1) {
            value = njs_argument(args, 1);

            if (njs_slow_path(!njs_is_number(value))) {
                ret = njs_value_to_integer(vm, value, &start);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }

            } else {
                start = njs_number_to_integer(njs_number(value));
            }

            if (start < 0) {
                start += length;

                if (start < 0) {
                    start = 0;
                }

            } else if (start > length) {
                start = length;
            }

            delete = length - start;

            if (nargs > 2) {
                value = njs_argument(args, 2);

                if (njs_slow_path(!njs_is_number(value))) {
                    ret = njs_value_to_integer(vm, value, &n);
                    if (njs_slow_path(ret != NJS_OK)) {
                        return ret;
                    }

                } else {
                    n = njs_number_to_integer(njs_number(value));
                }

                if (n < 0) {
                    delete = 0;

                } else if (n < delete) {
                    delete = n;
                }
            }
        }
    }

    deleted = njs_array_alloc(vm, delete, 0);
    if (njs_slow_path(deleted == NULL)) {
        return NJS_ERROR;
    }

    if (array != NULL && (delete >= 0 || nargs > 3)) {

        /* Move deleted items to a new array to return. */
        for (i = 0, n = start; i < (njs_uint_t) delete; i++, n++) {
            /* No retention required. */
            deleted->start[i] = array->start[n];
        }

        items = (nargs > 3) ? nargs - 3: 0;
        delta = items - delete;

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

            memmove(&array->start[start + items], &array->start[n],
                    (array->length - n) * sizeof(njs_value_t));

            array->length += delta;
        }

        /* Copy new items. */
        n = start;

        for (i = 3; i < nargs; i++) {
            /* GC: njs_retain(&args[i]); */
            array->start[n++] = args[i];
        }
    }

    njs_set_array(&vm->retval, deleted);

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_reverse(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t    ret;
    njs_uint_t   i, n, length;
    njs_value_t  value, *this;
    njs_array_t  *array;

    this = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_is_array(this)) {
        array = njs_array(this);
        length = array->length;

        if (length > 1) {
            for (i = 0, n = length - 1; i < n; i++, n--) {
                value = array->start[i];
                array->start[i] = array->start[n];
                array->start[n] = value;
            }
        }

        njs_set_array(&vm->retval, array);

    } else {
        /* STUB */
        vm->retval = *this;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_to_string(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t           ret;
    njs_value_t         value;
    njs_lvlhsh_query_t  lhq;

    if (njs_is_object(&args[0])) {
        njs_object_property_init(&lhq, "join", NJS_JOIN_HASH);

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
    u_char             *p;
    uint32_t           max;
    size_t             size, length, mask;
    njs_int_t          ret;
    njs_uint_t         i, n;
    njs_array_t        *array;
    njs_value_t        *value, *values;
    njs_string_prop_t  separator, string;

    ret = njs_value_to_object(vm, &args[0]);
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

    if (!njs_is_array(&args[0]) || njs_array_len(&args[0]) == 0) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    array = njs_array(&args[0]);

    max = 0;
    values = NULL;

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];

        if (!njs_is_string(value)
            && njs_is_valid(value)
            && !njs_is_null_or_undefined(value))
        {
            max++;
        }
    }

    if (max != 0) {
        values = njs_mp_align(vm->mem_pool, sizeof(njs_value_t),
                              sizeof(njs_value_t) * max);
        if (njs_slow_path(values == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        n = 0;

        for (i = 0; i < array->length; i++) {
            value = &array->start[i];

            if (!njs_is_string(value)
                && njs_is_valid(value)
                && !njs_is_null_or_undefined(value))
            {
                values[n++] = *value;

                if (n >= max) {
                    break;
                }
            }
        }
    }

    size = 0;
    length = 0;
    n = 0;
    mask = -1;

    array = njs_array(&args[0]);

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];

        if (njs_is_valid(value) && !njs_is_null_or_undefined(value)) {

            if (!njs_is_string(value)) {
                value = &values[n++];

                if (!njs_is_string(value)) {
                    ret = njs_value_to_string(vm, value, value);
                    if (ret != NJS_OK) {
                        return ret;
                    }
                }
            }

            (void) njs_string_prop(&string, value);

            size += string.size;
            length += string.length;

            if (string.length == 0 && string.size != 0) {
                mask = 0;
            }
        }
    }

    size += separator.size * (array->length - 1);
    length += separator.length * (array->length - 1);

    length &= mask;

    p = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    n = 0;

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];

        if (njs_is_valid(value) && !njs_is_null_or_undefined(value)) {
            if (!njs_is_string(value)) {
                value = &values[n++];
            }

            (void) njs_string_prop(&string, value);

            p = memcpy(p, string.start, string.size);
            p += string.size;
        }

        if (i < array->length - 1) {
            p = memcpy(p, separator.start, separator.size);
            p += separator.size;
        }
    }

    for (i = 0; i < max; i++) {
        njs_release(vm, &values[i]);
    }

    njs_mp_free(vm->mem_pool, values);

    return NJS_OK;
}


static int
njs_object_indexes_handler(const void *first, const void *second)
{
    double             num1, num2;
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

        return (int) (num1 - num2);
    }

    njs_string_get(val1, &str1);
    njs_string_get(val2, &str2);

    return strncmp((const char *) str1.start, (const char *) str2.start,
                   njs_min(str1.length, str2.length));
}


static njs_array_t *
njs_object_indexes(njs_vm_t *vm, njs_value_t *object)
{
    double       idx;
    uint32_t     i;
    njs_array_t  *keys;

    keys = njs_value_own_enumerate(vm, object, NJS_ENUM_KEYS, 0);
    if (njs_slow_path(keys == NULL)) {
        return NULL;
    }

    qsort(keys->start, keys->length, sizeof(njs_value_t),
          njs_object_indexes_handler);

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
njs_array_object_handler(njs_vm_t *vm, njs_array_iterator_handler_t handler,
    njs_array_iterator_args_t *args, njs_value_t *key, uint32_t i)
{
    njs_int_t    ret;
    njs_value_t  prop, *entry;

    ret = njs_value_property(vm, args->value, key, &prop);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    entry = (ret == NJS_OK) ? &prop : njs_value_arg(&njs_value_invalid);

    ret = handler(vm, args, entry, i);

    if (njs_slow_path(ret != NJS_OK)) {
        if (ret > 0) {
            return NJS_DECLINED;
        }

        return NJS_ERROR;
    }

    return ret;
}


njs_inline njs_int_t
njs_array_iterator(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_array_iterator_handler_t handler)
{
    double             idx;
    uint32_t           length, i, from, to;
    njs_int_t          ret;
    njs_array_t        *keys;
    njs_value_t        *value, character, index, string_obj;
    njs_object_t       *object;
    const u_char       *p, *end, *pos;
    njs_string_prop_t  string_prop;

    value = args->value;
    from = args->from;
    to = args->to;

    if (njs_is_array(value)) {
        if (njs_slow_path(!njs_object_hash_is_empty(value))) {
            goto process_object;
        }

        for (i = from; i < to; i++) {
            if (i < njs_array_len(value)) {
                ret = handler(vm, args, &njs_array_start(value)[i], i);

            } else {
                ret = handler(vm, args, njs_value_arg(&njs_value_invalid), i);
            }

            if (njs_slow_path(ret != NJS_OK)) {
                if (ret > 0) {
                    return NJS_DECLINED;
                }

                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    if (njs_is_string(value) || njs_is_object_string(value)) {

        if (njs_is_string(value)) {
            object = njs_object_value_alloc(vm, value, NJS_STRING);
            if (njs_slow_path(object == NULL)) {
                return NJS_ERROR;
            }

            njs_set_type_object(&string_obj, object, NJS_OBJECT_STRING);

            args->value = &string_obj;
        }
        else {
            value = njs_object_value(value);
        }

        length = (uint32_t) njs_string_prop(&string_prop, value);

        p = string_prop.start;
        end = p + string_prop.size;

        if (length == string_prop.size) {
            /* Byte or ASCII string. */

            for (i = from; i < to; i++) {
                /* This cannot fail. */
                (void) njs_string_new(vm, &character, p + i, 1, 1);

                ret = handler(vm, args, &character, i);
                if (njs_slow_path(ret != NJS_OK)) {
                    if (ret > 0) {
                        return NJS_DECLINED;
                    }

                    return NJS_ERROR;
                }
            }

        } else {
            /* UTF-8 string. */

            for (i = from; i < to; i++) {
                pos = njs_utf8_next(p, end);

                /* This cannot fail. */
                (void) njs_string_new(vm, &character, p, pos - p, 1);

                ret = handler(vm, args, &character, i);
                if (njs_slow_path(ret != NJS_OK)) {
                    if (ret > 0) {
                        return NJS_DECLINED;
                    }

                    return NJS_ERROR;
                }

                p = pos;
            }
        }

        return NJS_OK;
    }

    if (!njs_is_object(value)) {
        return NJS_OK;
    }

process_object:

    if ((to - from) > NJS_ARRAY_LARGE_OBJECT_LENGTH) {
        keys = njs_object_indexes(vm, value);
        if (njs_slow_path(keys == NULL)) {
            return NJS_ERROR;
        }

        for (i = 0; i < keys->length; i++) {
            idx = njs_string_to_index(&keys->start[i]);

            if (idx < from || idx > to) {
                continue;
            }

            ret = njs_array_object_handler(vm, handler, args, &keys->start[i],
                                           i);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        return NJS_OK;
    }

    for (i = from; i < to; i++) {
        njs_uint32_to_string(&index, i);

        ret = njs_array_object_handler(vm, handler, args, &index, i);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    return NJS_OK;
}


njs_inline njs_int_t
njs_array_reverse_iterator(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_array_iterator_handler_t handler)
{
    double             idx;
    uint32_t           i, from, to, length;
    njs_int_t          ret;
    njs_array_t        *keys;
    njs_value_t        *entry, *value, character, index, string_obj;
    njs_object_t       *object;
    const u_char       *p, *end, *pos;
    njs_string_prop_t  string_prop;

    value = args->value;
    from = args->from;
    to = args->to;

    if (njs_is_array(value)) {
        if (njs_slow_path(!njs_object_hash_is_empty(value))) {
            goto process_object;
        }

        i = from + 1;

        while (i-- > to) {
            entry = &njs_array_start(value)[i];

            ret = handler(vm, args, entry, i);
            if (njs_slow_path(ret != NJS_OK)) {
                if (ret > 0) {
                    return NJS_DECLINED;
                }

                return NJS_ERROR;
            }
        }

        return NJS_OK;
    }

    if (njs_is_string(value) || njs_is_object_string(value)) {

        if (njs_is_string(value)) {
            object = njs_object_value_alloc(vm, value, NJS_STRING);
            if (njs_slow_path(object == NULL)) {
                return NJS_ERROR;
            }

            njs_set_type_object(&string_obj, object, NJS_OBJECT_STRING);

            args->value = &string_obj;
        }
        else {
            value = njs_object_value(value);
        }

        length = (uint32_t) njs_string_prop(&string_prop, value);
        end = string_prop.start + string_prop.size;

        if (length == string_prop.size) {
            /* Byte or ASCII string. */

            p = string_prop.start + from;

            i = from + 1;

            while (i-- > to) {
                /* This cannot fail. */
                (void) njs_string_new(vm, &character, p, 1, 1);

                ret = handler(vm, args, &character, i);
                if (njs_slow_path(ret != NJS_OK)) {
                    if (ret > 0) {
                        return NJS_DECLINED;
                    }

                    return NJS_ERROR;
                }

                p--;
            }

        } else {
            /* UTF-8 string. */

            p = njs_string_offset(string_prop.start, end, from);
            p = njs_utf8_next(p, end);

            i = from + 1;

            while (i-- > to) {
                pos = njs_utf8_prev(p);

                /* This cannot fail. */
                (void) njs_string_new(vm, &character, pos, p - pos , 1);

                ret = handler(vm, args, &character, i);
                if (njs_slow_path(ret != NJS_OK)) {
                    if (ret > 0) {
                        return NJS_DECLINED;
                    }

                    return NJS_ERROR;
                }

                p = pos;
            }
        }

        return NJS_OK;
    }

    if (!njs_is_object(value)) {
        return NJS_OK;
    }

process_object:

    if ((from - to) > NJS_ARRAY_LARGE_OBJECT_LENGTH) {
        keys = njs_object_indexes(vm, value);
        if (njs_slow_path(keys == NULL)) {
            return NJS_ERROR;
        }

        i = keys->length;

        while (i > 0) {
            idx = njs_string_to_index(&keys->start[--i]);

            if (idx < to || idx > from) {
                continue;
            }

            ret = njs_array_object_handler(vm, handler, args, &keys->start[i],
                                           idx);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        return NJS_OK;
    }

    i = from + 1;

    while (i-- > to) {
        njs_uint32_to_string(&index, i);

        ret = njs_array_object_handler(vm, handler, args, &index, i);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_concat(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint64_t     length;
    njs_int_t    ret;
    njs_uint_t   i;
    njs_value_t  *value;
    njs_array_t  *array;

    ret = njs_value_to_object(vm, &args[0]);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = 0;

    for (i = 0; i < nargs; i++) {
        if (njs_is_array(&args[i])) {
            length += njs_array_len(&args[i]);

        } else {
            length++;
        }
    }

    array = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    njs_set_array(&vm->retval, array);

    value = array->start;

    for (i = 0; i < nargs; i++) {
        value = njs_array_copy(value, &args[i]);
    }

    return NJS_OK;
}


static njs_value_t *
njs_array_copy(njs_value_t *dst, njs_value_t *src)
{
    njs_uint_t  n;

    n = 1;

    if (njs_is_array(src)) {
        n = njs_array_len(src);
        src = njs_array_start(src);
    }

    while (n != 0) {
        /* GC: njs_retain src */
        *dst++ = *src++;
        n--;
    }

    return dst;
}


static njs_int_t
njs_array_handler_index_of(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
{
    if (njs_values_strict_equal(args->argument, entry)) {
        njs_set_number(&vm->retval, n);

        return 1;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_index_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t                    from;
    uint32_t                   length;
    njs_int_t                  ret;
    njs_array_iterator_args_t  iargs;

    iargs.value = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, iargs.value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    iargs.argument = njs_arg(args, nargs, 1);

    ret = njs_value_length(vm, iargs.value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &from);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (length == 0 || from >= (int64_t) length) {
        goto not_found;
    }

    if (from < 0) {
        from = length + from;

        if (from < 0) {
            from = 0;
        }
    }

    iargs.from = (uint32_t) from;
    iargs.to = length;

    ret = njs_array_iterator(vm, &iargs, njs_array_handler_index_of);
    if (njs_fast_path(ret == NJS_DECLINED)) {
        return NJS_OK;
    }

not_found:

    njs_set_number(&vm->retval, -1);

    return ret;
}


static njs_int_t
njs_array_prototype_last_index_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    int64_t                    from;
    uint32_t                   length;
    njs_int_t                  ret;
    njs_array_iterator_args_t  iargs;

    iargs.value = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, iargs.value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    iargs.argument = njs_arg(args, nargs, 1);

    ret = njs_value_length(vm, iargs.value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (length == 0) {
        goto not_found;
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

        if (from <= 0) {
            goto not_found;
        }
    }

    iargs.from = from;
    iargs.to = 0;

    ret = njs_array_reverse_iterator(vm, &iargs, njs_array_handler_index_of);
    if (njs_fast_path(ret == NJS_DECLINED)) {
        return NJS_OK;
    }

not_found:

    njs_set_number(&vm->retval, -1);

    return ret;
}


static njs_int_t
njs_array_handler_includes(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
{
    if (!njs_is_valid(entry)) {
        entry = njs_value_arg(&njs_value_undefined);
    }

    if (njs_values_strict_equal(args->argument, entry)) {
        njs_set_true(&vm->retval);

        return 1;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_includes_nan(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
{
    if (njs_is_numeric(entry) && isnan(njs_number(entry))) {
        njs_set_true(&vm->retval);

        return 1;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_includes(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t                    from;
    uint32_t                   length;
    njs_int_t                  ret;
    njs_array_iterator_args_t  iargs;

    iargs.value = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, iargs.value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    iargs.argument = njs_arg(args, nargs, 1);

    ret = njs_value_length(vm, iargs.value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (length == 0) {
        goto not_found;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &from);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (from < 0) {
        from += length;

        if (from < 0) {
            from = 0;
        }
    }

    iargs.from = (uint32_t) from;
    iargs.to = length;

    if (njs_is_number(iargs.argument) && isnan(njs_number(iargs.argument))) {
        ret = njs_array_iterator(vm, &iargs, njs_array_handler_includes_nan);
        if (njs_fast_path(ret == NJS_DECLINED)) {
            return NJS_OK;
        }

    } else {
        ret = njs_array_iterator(vm, &iargs, njs_array_handler_includes);
        if (njs_fast_path(ret == NJS_DECLINED)) {
            return NJS_OK;
        }
    }

not_found:

    njs_set_false(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_fill(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t       start, end;
    uint32_t      length;
    njs_int_t     i, ret;
    njs_array_t   *array;
    njs_value_t   name, *this, *value;

    this = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    array = NULL;

    if (njs_is_array(this)) {
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
        njs_uint32_to_string(&name, start++);

        ret = njs_value_property_set(vm, this, &name, value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    vm->retval = *this;

    return NJS_OK;
}


njs_inline njs_int_t
njs_array_iterator_call(njs_vm_t *vm, njs_array_iterator_args_t *args,
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


njs_inline njs_int_t
njs_array_validate_args(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_array_iterator_args_t *iargs)
{
    njs_int_t  ret;

    iargs->value = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, iargs->value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_length(vm, iargs->value, &iargs->to);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(!njs_is_function(njs_arg(args, nargs, 1)))) {
        goto failed;
    }

    iargs->from = 0;
    iargs->function = njs_function(njs_argument(args, 1));
    iargs->argument = njs_arg(args, nargs, 2);

    return NJS_OK;

failed:

    njs_type_error(vm, "unexpected iterator arguments");

    return NJS_ERROR;
}


static njs_int_t
njs_array_handler_for_each(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
{
    if (njs_is_valid(entry)) {
        return njs_array_iterator_call(vm, args, entry, n);
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_for_each(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                  ret;
    njs_array_iterator_args_t  iargs;

    ret = njs_array_validate_args(vm, args, nargs, &iargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_array_iterator(vm, &iargs, njs_array_handler_for_each);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_array_handler_some(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
{
    njs_int_t  ret;

    if (njs_is_valid(entry)) {
        ret = njs_array_iterator_call(vm, args, entry, n);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_true(&vm->retval)) {
            vm->retval = njs_value_true;

            return 1;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_some(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                  ret;
    njs_array_iterator_args_t  iargs;

    ret = njs_array_validate_args(vm, args, nargs, &iargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_array_iterator(vm, &iargs, njs_array_handler_some);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret != NJS_DECLINED) {
        vm->retval = njs_value_false;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_every(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
{
    njs_int_t  ret;

    if (njs_is_valid(entry)) {
        ret = njs_array_iterator_call(vm, args, entry, n);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (!njs_is_true(&vm->retval)) {
            vm->retval = njs_value_false;

            return 1;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_every(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                  ret;
    njs_array_iterator_args_t  iargs;

    ret = njs_array_validate_args(vm, args, nargs, &iargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_array_iterator(vm, &iargs, njs_array_handler_every);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret != NJS_DECLINED) {
        vm->retval = njs_value_true;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_filter(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
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

            ret = njs_array_add(vm, args->array, &copy);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_filter(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                  ret;
    njs_array_iterator_args_t  iargs;

    ret = njs_array_validate_args(vm, args, nargs, &iargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    iargs.array = njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(iargs.array == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_array_iterator(vm, &iargs, njs_array_handler_filter);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_array(&vm->retval, iargs.array);

    return NJS_OK;
}


static njs_int_t
njs_array_handler_find(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
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

        return 1;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_find(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                  ret;
    njs_array_iterator_args_t  iargs;

    ret = njs_array_validate_args(vm, args, nargs, &iargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_array_iterator(vm, &iargs, njs_array_handler_find);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret != NJS_DECLINED) {
        vm->retval = njs_value_undefined;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_find_index(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
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

        return 1;
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_find_index(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t                  ret;
    njs_array_iterator_args_t  iargs;

    ret = njs_array_validate_args(vm, args, nargs, &iargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_array_iterator(vm, &iargs, njs_array_handler_find_index);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret != NJS_DECLINED) {
        njs_set_number(&vm->retval, -1);
    }

    return NJS_OK;
}


static njs_int_t
njs_array_handler_map(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
{
    njs_int_t    ret;
    njs_array_t  *retval;

    retval = args->array;

    njs_set_invalid(&retval->start[n]);

    if (njs_is_valid(entry)) {
        ret = njs_array_iterator_call(vm, args, entry, n);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_valid(&vm->retval)) {
            retval->start[n] = vm->retval;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_map(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t                   length, i;
    njs_int_t                  ret;
    njs_array_t                *array;
    njs_array_iterator_args_t  iargs;

    iargs.value = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, iargs.value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_length(vm, iargs.value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(!njs_is_function(njs_arg(args, nargs, 1)))) {
        goto unexpected_args;
    }

    iargs.array = njs_array_alloc(vm, length, 0);
    if (njs_slow_path(iargs.array == NULL)) {
        return NJS_ERROR;
    }

    if (length > NJS_ARRAY_LARGE_OBJECT_LENGTH) {
        for (i = 0; i < length; i++) {
            njs_set_invalid(&iargs.array->start[i]);
        }
    }

    if (length > 0) {
        iargs.from = 0;
        iargs.to = length;

        iargs.function = njs_function(njs_argument(args, 1));
        iargs.argument = njs_arg(args, nargs, 2);

        ret = njs_array_iterator(vm, &iargs, njs_array_handler_map);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_array(&args[0]) && njs_object_hash_is_empty(&args[0])) {
            array = iargs.array;

            for (i = njs_array_len(&args[0]); i < length; i++) {
                njs_set_invalid(&array->start[i]);
            }
        }
    }

    njs_set_array(&vm->retval, iargs.array);

    return NJS_OK;

unexpected_args:

    njs_type_error(vm, "unexpected iterator arguments");

    return NJS_ERROR;
}


njs_inline njs_int_t
njs_array_iterator_reduce(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
{
    njs_value_t  arguments[5];

    /* GC: array elt, array */

    njs_set_undefined(&arguments[0]);
    arguments[1] = *args->argument;
    arguments[2] = *entry;
    njs_set_number(&arguments[3], n);
    arguments[4] = *args->value;

    return njs_function_apply(vm, args->function, arguments, 5, args->argument);
}


static njs_int_t
njs_array_handler_reduce(njs_vm_t *vm, njs_array_iterator_args_t *args,
    njs_value_t *entry, uint32_t n)
{
    njs_int_t  ret;

    if (njs_is_valid(entry)) {

        if (!njs_is_valid(args->argument)) {
            *(args->argument) = *entry;
            return NJS_OK;
        }

        ret = njs_array_iterator_reduce(vm, args, entry, n);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_reduce(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                  ret;
    njs_value_t                accumulator;
    njs_array_iterator_args_t  iargs;

    ret = njs_array_validate_args(vm, args, nargs, &iargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_invalid(&accumulator);

    if (nargs > 2) {
        accumulator = *iargs.argument;
    }

    iargs.argument = &accumulator;

    ret = njs_array_iterator(vm, &iargs, njs_array_handler_reduce);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!njs_is_valid(&accumulator)) {
        njs_type_error(vm, "Reduce of empty object with no initial value");
        return NJS_ERROR;
    }

    vm->retval = accumulator;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_reduce_right(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t                  ret;
    njs_value_t                accumulator;
    njs_array_iterator_args_t  iargs;

    iargs.value = njs_arg(args, nargs, 0);

    ret = njs_value_to_object(vm, iargs.value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_length(vm, iargs.value, &iargs.from);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(!njs_is_function(njs_arg(args, nargs, 1)))) {
        goto unexpected_args;
    }

    njs_set_invalid(&accumulator);

    iargs.to = 0;
    iargs.function = njs_function(njs_argument(args, 1));
    iargs.argument = &accumulator;

    if (nargs > 2) {
        accumulator = *njs_argument(args, 2);
    }

    if (iargs.from == 0) {
        if (nargs < 3) {
            goto failed;
        }

        goto done;
    }

    iargs.from--;

    ret = njs_array_reverse_iterator(vm, &iargs, njs_array_handler_reduce);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!njs_is_valid(&accumulator)) {
        goto failed;
    }

done:

    vm->retval = accumulator;

    return NJS_OK;

failed:

    njs_type_error(vm, "Reduce of empty object with no initial value");

    return NJS_ERROR;

unexpected_args:

    njs_type_error(vm, "unexpected iterator arguments");

    return NJS_ERROR;
}


static njs_int_t
njs_array_string_sort(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t   ret;
    njs_uint_t  i;

    for (i = 1; i < nargs; i++) {
        if (!njs_is_string(&args[i])) {
            ret = njs_value_to_string(vm, &args[i], &args[i]);
            if (ret != NJS_OK) {
                return ret;
            }
        }
    }

    ret = njs_string_cmp(&args[1], &args[2]);

    njs_set_number(&vm->retval, ret);

    return NJS_OK;
}


static const njs_function_t  njs_array_string_sort_function = {
    .object = { .type = NJS_FUNCTION, .shared = 1, .extensible = 1 },
    .native = 1,
    .args_offset = 1,
    .u.native = njs_array_string_sort,
};


static njs_int_t
njs_array_prototype_sort(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    uint32_t        n, index, current;
    njs_int_t       ret;
    njs_array_t     *array;
    njs_value_t     retval, value, *start, arguments[3];;
    njs_function_t  *function;

    if (!njs_is_array(&args[0]) || njs_array_len(&args[0]) == 0) {
        vm->retval = args[0];
        return NJS_OK;
    }

    if (nargs > 1 && njs_is_function(&args[1])) {
        function = njs_function(&args[1]);

    } else {
        function = (njs_function_t *) &njs_array_string_sort_function;
    }

    index = 0;
    current = 0;
    retval = njs_value_zero;
    array = njs_array(&args[0]);
    start = array->start;

start:

    if (njs_is_number(&retval)) {

        /*
         * The sort function is implemented with the insertion sort algorithm.
         * Its worst and average computational complexity is O^2.  This point
         * should be considered as return point from comparison function so
         * "goto next" moves control to the appropriate step of the algorithm.
         * The first iteration also goes there because sort->retval is zero.
         */
        if (njs_number(&retval) <= 0) {
            goto next;
        }

        n = index;

    swap:

        value = start[n];
        start[n] = start[n - 1];
        n--;
        start[n] = value;

        do {
            if (n > 0) {

                if (njs_is_valid(&start[n])) {

                    if (njs_is_valid(&start[n - 1])) {
                        njs_set_undefined(&arguments[0]);

                        /* GC: array elt, array */
                        arguments[1] = start[n - 1];
                        arguments[2] = start[n];

                        index = n;

                        ret = njs_function_apply(vm, function, arguments, 3,
                                                 &retval);

                        if (njs_slow_path(ret != NJS_OK)) {
                            return ret;
                        }

                        goto start;
                    }

                    /* Move invalid values to the end of array. */
                    goto swap;
                }
            }

        next:

            current++;
            n = current;

        } while (n < array->length);
    }

    vm->retval = args[0];

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_copy_within(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    int8_t       direction;
    int64_t      count, to, from, end;
    uint32_t     length;
    njs_int_t    ret;
    njs_array_t  *array;
    njs_value_t  *this, *value, from_key, to_key, prop;

    this = njs_arg(args, nargs, 0);

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

    if (from < to && from + count) {
        direction = -1;
        from = from + count - 1;
        to = to + count - 1;

    } else {
        direction = 1;
    }

    njs_vm_retval_set(vm, this);

    if (njs_is_array(this)) {
        if (njs_slow_path(!njs_object_hash_is_empty(this))) {
            goto process_object;
        }

        array = njs_array(this);

        while (count-- > 0) {
            array->start[to] = array->start[from];

            from = from + direction;
            to = to + direction;
        }

        return NJS_OK;
    }

process_object:

    while (count-- > 0) {
        /* FIXME: largest index is 2**53-1. */

        njs_uint32_to_string(&from_key, (uint32_t) from);
        njs_uint32_to_string(&to_key, (uint32_t) to);

        ret = njs_value_property(vm, this, &from_key, &prop);

        if (ret == NJS_OK) {
            ret = njs_value_property_set(vm, this, &to_key, &prop);

        } else {
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            ret = njs_value_property_delete(vm, this, &to_key, NULL);
        }

        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        from = from + direction;
        to = to + direction;
    }

    return NJS_OK;
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
        .name = njs_string("slice"),
        .value = njs_native_function(njs_array_prototype_slice, 2),
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
        .name = njs_string("pop"),
        .value = njs_native_function(njs_array_prototype_pop, 0),
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
        .name = njs_string("shift"),
        .value = njs_native_function(njs_array_prototype_shift, 0),
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
        .name = njs_string("reverse"),
        .value = njs_native_function(njs_array_prototype_reverse, 0),
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
        .name = njs_string("join"),
        .value = njs_native_function(njs_array_prototype_join, 1),
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
        .name = njs_string("indexOf"),
        .value = njs_native_function(njs_array_prototype_index_of, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("lastIndexOf"),
        .value = njs_native_function(njs_array_prototype_last_index_of, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES7. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("includes"),
        .value = njs_native_function(njs_array_prototype_includes, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("forEach"),
        .value = njs_native_function(njs_array_prototype_for_each, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("some"),
        .value = njs_native_function(njs_array_prototype_some, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("every"),
        .value = njs_native_function(njs_array_prototype_every, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
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
        .value = njs_native_function(njs_array_prototype_filter, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("find"),
        .value = njs_native_function(njs_array_prototype_find, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("findIndex"),
        .value = njs_native_function(njs_array_prototype_find_index, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("map"),
        .value = njs_native_function(njs_array_prototype_map, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("reduce"),
        .value = njs_native_function(njs_array_prototype_reduce, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("reduceRight"),
        .value = njs_native_function(njs_array_prototype_reduce_right, 1),
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
        .name = njs_string("copyWithin"),
        .value = njs_native_function(njs_array_prototype_copy_within, 2),
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
    .constructor = njs_array_constructor,
    .prototype_props = &njs_array_prototype_init,
    .constructor_props = &njs_array_constructor_init,
    .value = { .object = { .type = NJS_ARRAY } },
};
