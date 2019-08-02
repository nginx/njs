
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t njs_array_prototype_slice_copy(njs_vm_t *vm,
    njs_value_t *this, int64_t start, int64_t length);
static njs_value_t *njs_array_copy(njs_value_t *dst, njs_value_t *src);


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
    array->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_ARRAY].object;
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

    memcpy(start, array->start, array->length * sizeof(njs_value_t));

    array->start = start;

    njs_mp_free(vm->mem_pool, old);

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


njs_int_t
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
        .type = NJS_METHOD,
        .name = njs_string("isArray"),
        .value = njs_native_function(njs_array_is_array, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    /* Array.of(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("of"),
        .value = njs_native_function(njs_array_of, 0),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_array_constructor_init = {
    njs_str("Array"),
    njs_array_constructor_properties,
    njs_nitems(njs_array_constructor_properties),
};


static njs_int_t
njs_array_length(njs_vm_t *vm, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval)
{
    double       num;
    int64_t      size;
    uint32_t     length;
    njs_int_t    ret;
    njs_value_t  *val;
    njs_array_t  *array;
    njs_object_t *proto;
    njs_value_t  val_length;

    proto = njs_object(value);

    if (njs_fast_path(setval == NULL)) {
        do {
            if (njs_fast_path(proto->type == NJS_ARRAY)) {
                break;
            }

            proto = proto->__proto__;
        } while (proto != NULL);

        if (njs_slow_path(proto == NULL)) {
            njs_internal_error(vm, "no array in proto chain");
            return NJS_ERROR;
        }

        array = (njs_array_t *) proto;

        njs_set_number(retval, array->length);
        return NJS_OK;
    }

    if (proto->type != NJS_ARRAY) {
        return NJS_DECLINED;
    }

    if (njs_slow_path(!njs_is_number(setval))) {
        ret = njs_value_to_numeric(vm, &val_length, setval);
        if (ret != NJS_OK) {
            return ret;
        }

        num = njs_number(&val_length);

    } else {
        num = njs_number(setval);
    }

    length = njs_number_to_uint32(num);

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
    njs_int_t    ret;
    njs_value_t  prop_length;

    static const njs_value_t  string_length = njs_string("length");

    ret = njs_value_property(vm, &args[0], njs_value_arg(&string_length),
                             &prop_length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_slow_path(!njs_is_primitive(&prop_length))) {
        ret = njs_value_to_numeric(vm, &prop_length, &prop_length);
        if (ret != NJS_OK) {
            return ret;
        }
    }

    start = njs_primitive_value_to_integer(njs_arg(args, nargs, 1));
    length = njs_primitive_value_to_length(&prop_length);

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
        if (!njs_is_undefined(njs_arg(args, nargs, 2))) {
            end = njs_primitive_value_to_integer(&args[2]);

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

    return njs_array_prototype_slice_copy(vm, &args[0], start, length);
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
                    *value = njs_value_invalid;
                }

                length--;
            } while (length != 0);

        } else {

            /* Primitive types. */

            value = array->start;

            do {
                *value++ = njs_value_invalid;
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
    njs_int_t    ret;
    njs_uint_t   i;
    njs_array_t  *array;

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
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_pop(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_array_t        *array;
    const njs_value_t  *retval, *value;

    retval = &njs_value_undefined;

    if (njs_is_array(&args[0])) {
        array = njs_array(&args[0]);

        if (array->length != 0) {
            array->length--;
            value = &array->start[array->length];

            if (njs_is_valid(value)) {
                retval = value;
            }
        }
    }

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_unshift(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t    ret;
    njs_uint_t   n;
    njs_array_t  *array;

    if (njs_is_array(&args[0])) {
        array = njs_array(&args[0]);
        n = nargs - 1;

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
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_shift(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_array_t        *array;
    const njs_value_t  *retval, *value;

    retval = &njs_value_undefined;

    if (njs_is_array(&args[0])) {
        array = njs_array(&args[0]);

        if (array->length != 0) {
            array->length--;

            value = &array->start[0];
            array->start++;

            if (njs_is_valid(value)) {
                retval = value;
            }
        }
    }

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_splice(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t    ret;
    njs_int_t    n, start, length, items, delta, delete;
    njs_uint_t   i;
    njs_array_t  *array, *deleted;

    array = NULL;
    start = 0;
    delete = 0;

    if (njs_is_array(&args[0])) {
        array = njs_array(&args[0]);
        length = array->length;

        if (nargs > 1) {
            start = njs_number(&args[1]);

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
                n = njs_number(&args[2]);

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

        items = nargs - 3;

        if (items < 0) {
            items = 0;
        }

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
    njs_uint_t   i, n, length;
    njs_value_t  value;
    njs_array_t  *array;

    if (njs_is_array(&args[0])) {
        array = njs_array(&args[0]);
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
        vm->retval = args[0];
    }

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_to_string(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    if (njs_is_object(&args[0])) {
        lhq.key_hash = NJS_JOIN_HASH;
        lhq.key = njs_str_value("join");

        prop = njs_object_property(vm, njs_object(&args[0]), &lhq);

        if (njs_fast_path(prop != NULL && njs_is_function(&prop->value))) {
            return njs_function_apply(vm, njs_function(&prop->value), args,
                                      nargs, &vm->retval);
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

    if (nargs > 1) {
        value = &args[1];

    } else {
        value = njs_value_arg(&njs_string_comma);
    }

    (void) njs_string_prop(&separator, value);

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


static njs_int_t
njs_array_prototype_concat(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint64_t     length;
    njs_uint_t   i;
    njs_value_t  *value;
    njs_array_t  *array;

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
njs_array_prototype_index_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t    i, index, length;
    njs_value_t  *value, *start;
    njs_array_t  *array;

    index = -1;

    if (nargs < 2 || !njs_is_array(&args[0])) {
        goto done;
    }

    array = njs_array(&args[0]);
    length = array->length;

    if (length == 0) {
        goto done;
    }

    i = 0;

    if (nargs > 2) {
        i = njs_number(&args[2]);

        if (i >= length) {
            goto done;
        }

        if (i < 0) {
            i += length;

            if (i < 0) {
                i = 0;
            }
        }
    }

    value = &args[1];
    start = array->start;

    do {
        if (njs_values_strict_equal(value, &start[i])) {
            index = i;
            break;
        }

        i++;

    } while (i < length);

done:

    njs_set_number(&vm->retval, index);

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_last_index_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t    k, n, index, length;
    njs_value_t  *start;
    njs_array_t  *array;
    njs_value_t  *this, *value;

    index = -1;

    this = njs_arg(args, nargs, 0);

    if (!njs_is_array(this)) {
        goto done;
    }

    array = njs_array(this);
    length = array->length;

    if (length == 0) {
        goto done;
    }

    if (nargs > 2) {
        n = njs_primitive_value_to_integer(njs_argument(args, 2));

    } else {
        n = length - 1;
    }

    if (n >= 0) {
        k = njs_min(n, length - 1);

    } else {
        k = n + length;

        if (k < 0) {
            goto done;
        }
    }

    value = njs_arg(args, nargs, 1);
    start = array->start;

    do {
        if (njs_values_strict_equal(value, &start[k])) {
            index = k;
            break;
        }

        k--;

    } while (k >= 0);

done:

    njs_set_number(&vm->retval, index);

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_includes(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t          i, length;
    njs_value_t        *value, *start;
    njs_array_t        *array;
    const njs_value_t  *retval;

    retval = &njs_value_false;

    if (nargs < 2 || !njs_is_array(&args[0])) {
        goto done;
    }

    array = njs_array(&args[0]);
    length = array->length;

    if (length == 0) {
        goto done;
    }

    i = 0;

    if (nargs > 2) {
        i = njs_number(&args[2]);

        if (i >= length) {
            goto done;
        }

        if (i < 0) {
            i += length;

            if (i < 0) {
                i = 0;
            }
        }
    }

    start = array->start;
    value = &args[1];

    if (njs_is_number(value) && isnan(njs_number(value))) {

        do {
            value = &start[i];

            if (njs_is_number(value) && isnan(njs_number(value))) {
                retval = &njs_value_true;
                break;
            }

            i++;

        } while (i < length);

    } else {
        do {
            if (njs_values_strict_equal(value, &start[i])) {
                retval = &njs_value_true;
                break;
            }

            i++;

        } while (i < length);
    }

done:

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_fill(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t     ret;
    njs_int_t     i, start, end, length;
    njs_array_t   *array;
    njs_value_t   name, prop_length, *this, *value;
    njs_object_t  *object;

    static const njs_value_t  string_length = njs_string("length");

    this = njs_arg(args, nargs, 0);

    if (njs_is_primitive(this)) {
        if (njs_is_null_or_undefined(this)) {
            njs_type_error(vm, "\"this\" argument cannot be "
                               "undefined or null value");
            return NJS_ERROR;
        }

        object = njs_object_value_alloc(vm, this, this->type);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        njs_set_type_object(&vm->retval, object, object->type);

        return NJS_OK;
    }

    array = NULL;

    if (njs_is_array(this)) {
        array = njs_array(this);
        length = array->length;

    } else {
        ret = njs_value_property(vm, this, njs_value_arg(&string_length),
                                 &prop_length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (njs_slow_path(!njs_is_primitive(&prop_length))) {
            ret = njs_value_to_numeric(vm, &prop_length, &prop_length);
            if (ret != NJS_OK) {
                return ret;
            }
        }

        length = njs_primitive_value_to_length(&prop_length);
    }

    start = njs_primitive_value_to_integer(njs_arg(args, nargs, 2));
    start = (start < 0) ? njs_max(length + start, 0) : njs_min(start, length);

    if (njs_is_undefined(njs_arg(args, nargs, 3))) {
        end = length;

    } else {
        end = njs_primitive_value_to_integer(njs_arg(args, nargs, 3));
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
njs_array_iterator_call(njs_vm_t *vm, njs_function_t *function,
    const njs_value_t *this_arg, const njs_value_t *value, uint32_t n,
    const njs_value_t *array)
{
    njs_value_t  arguments[3];

    /* GC: array elt, array */

    arguments[0] = *value;
    njs_set_number(&arguments[1], n);
    arguments[2] = *array;

    return njs_function_call(vm, function, this_arg, arguments, 3,
                             &vm->retval);
}


static njs_int_t
njs_array_prototype_for_each(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t           i, length;
    njs_int_t          ret;
    njs_value_t        *array, *value, *this_arg;
    njs_function_t     *function;

    if (nargs < 2 || !njs_is_array(&args[0]) || !njs_is_function(&args[1])) {
        njs_type_error(vm, "unexpected iterator arguments");
        return NJS_ERROR;
    }

    array = &args[0];
    length = njs_array_len(array);
    function = njs_function(&args[1]);
    this_arg = njs_arg(args, nargs, 2);

    for (i = 0; i < length; i++) {
        value = &njs_array_start(array)[i];

        if (njs_is_valid(value)) {
            ret = njs_array_iterator_call(vm, function, this_arg, value, i,
                                          array);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        length = njs_min(length, njs_array_len(array));
    }

    vm->retval = njs_value_undefined;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_some(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t           i, length;
    njs_int_t          ret;
    njs_value_t        *array, *value, *this_arg;
    njs_function_t     *function;
    const njs_value_t  *retval;

    if (nargs < 2 || !njs_is_array(&args[0]) || !njs_is_function(&args[1])) {
        njs_type_error(vm, "unexpected iterator arguments");
        return NJS_ERROR;
    }

    array = &args[0];
    length = njs_array_len(array);
    function = njs_function(&args[1]);
    this_arg = njs_arg(args, nargs, 2);

    retval = &njs_value_false;

    for (i = 0; i < length; i++) {
        value = &njs_array_start(array)[i];

        if (njs_is_valid(value)) {
            ret = njs_array_iterator_call(vm, function, this_arg, value, i,
                                          array);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (njs_is_true(&vm->retval)) {
                retval = &njs_value_true;
                break;
            }
        }

        length = njs_min(length, njs_array_len(array));
    }

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_every(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t           i, length;
    njs_int_t          ret;
    njs_value_t        *array, *value, *this_arg;
    njs_function_t     *function;
    const njs_value_t  *retval;

    if (nargs < 2 || !njs_is_array(&args[0]) || !njs_is_function(&args[1])) {
        njs_type_error(vm, "unexpected iterator arguments");
        return NJS_ERROR;
    }

    array = &args[0];
    length = njs_array_len(array);
    function = njs_function(&args[1]);
    this_arg = njs_arg(args, nargs, 2);

    retval = &njs_value_true;

    for (i = 0; i < length; i++) {
        value = &njs_array_start(array)[i];

        if (njs_is_valid(value)) {
            ret = njs_array_iterator_call(vm, function, this_arg, value, i,
                                          array);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (!njs_is_true(&vm->retval)) {
                retval = &njs_value_false;
                break;
            }
        }

        length = njs_min(length, njs_array_len(array));
    }

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_filter(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t        i, length;
    njs_int_t       ret;
    njs_array_t     *retval;
    njs_value_t     *array, value, *this_arg;
    njs_function_t  *function;

    if (nargs < 2 || !njs_is_array(&args[0]) || !njs_is_function(&args[1])) {
        njs_type_error(vm, "unexpected iterator arguments");
        return NJS_ERROR;
    }

    array = &args[0];
    length = njs_array_len(array);
    function = njs_function(&args[1]);
    this_arg = njs_arg(args, nargs, 2);

    retval = njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(retval == NULL)) {
        return NJS_ERROR;
    }

    for (i = 0; i < length; i++) {
        value = njs_array_start(array)[i];

        if (njs_is_valid(&value)) {
            ret = njs_array_iterator_call(vm, function, this_arg, &value, i,
                                          array);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (njs_is_true(&vm->retval)) {
                ret = njs_array_add(vm, retval, &value);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }
            }
        }

        length = njs_min(length, njs_array_len(array));
    }

    njs_set_array(&vm->retval, retval);

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_find(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t           i, length;
    njs_int_t          ret;
    njs_value_t        *array, value, *this_arg;
    njs_function_t     *function;
    const njs_value_t  *retval;

    if (nargs < 2 || !njs_is_array(&args[0]) || !njs_is_function(&args[1])) {
        njs_type_error(vm, "unexpected iterator arguments");
        return NJS_ERROR;
    }

    array = &args[0];
    length = njs_array_len(array);
    function = njs_function(&args[1]);
    this_arg = njs_arg(args, nargs, 2);

    retval = &njs_value_undefined;

    for (i = 0; i < length; i++) {
        value = njs_array_start(array)[i];

        if (!njs_is_valid(&value)) {
            value = njs_value_undefined;
        }

        ret = njs_array_iterator_call(vm, function, this_arg, &value, i, array);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_true(&vm->retval)) {
            retval = &value;
            break;
        }

        length = njs_min(length, njs_array_len(array));
    }

    vm->retval = *retval;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_find_index(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double          index;
    uint32_t        i, length;
    njs_int_t       ret;
    njs_value_t     *array, value, *this_arg;
    njs_function_t  *function;

    if (nargs < 2 || !njs_is_array(&args[0]) || !njs_is_function(&args[1])) {
        njs_type_error(vm, "unexpected iterator arguments");
        return NJS_ERROR;
    }

    array = &args[0];
    length = njs_array_len(array);
    function = njs_function(&args[1]);
    this_arg = njs_arg(args, nargs, 2);

    index = -1;

    for (i = 0; i < length; i++) {
        value = njs_array_start(array)[i];

        if (!njs_is_valid(&value)) {
            value = njs_value_undefined;
        }

        ret = njs_array_iterator_call(vm, function, this_arg, &value, i, array);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_true(&vm->retval)) {
            index = i;
            break;
        }

        length = njs_min(length, njs_array_len(array));
    }

    njs_set_number(&vm->retval, index);

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_map(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t        i, length, size;
    njs_int_t       ret;
    njs_array_t     *retval;
    njs_value_t     *array, *value, *this_arg;
    njs_function_t  *function;

    if (nargs < 2 || !njs_is_array(&args[0]) || !njs_is_function(&args[1])) {
        njs_type_error(vm, "unexpected iterator arguments");
        return NJS_ERROR;
    }

    array = &args[0];
    length = njs_array_len(array);
    function = njs_function(&args[1]);
    this_arg = njs_arg(args, nargs, 2);

    size = length;

    retval = njs_array_alloc(vm, length, 0);
    if (njs_slow_path(retval == NULL)) {
        return NJS_ERROR;
    }

    for (i = 0; i < length; i++) {
        njs_set_invalid(&retval->start[i]);

        value = &njs_array_start(array)[i];

        if (njs_is_valid(value)) {
            ret = njs_array_iterator_call(vm, function, this_arg, value, i,
                                          array);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            if (njs_is_valid(&vm->retval)) {
                retval->start[i] = vm->retval;
            }
        }

        length = njs_min(length, njs_array_len(array));
    }

    for ( ; i < size; i++) {
        njs_set_invalid(&retval->start[i]);
    }

    njs_set_array(&vm->retval, retval);

    return NJS_OK;
}


njs_inline njs_int_t
njs_array_iterator_reduce(njs_vm_t *vm, njs_function_t *function,
    njs_value_t *accumulator, njs_value_t *value, uint32_t n,
    njs_value_t *array)
{
    njs_value_t  arguments[5];

    /* GC: array elt, array */

    arguments[0] = njs_value_undefined;
    arguments[1] = *accumulator;
    arguments[2] = *value;
    njs_set_number(&arguments[3], n);
    arguments[4] = *array;

    return njs_function_apply(vm, function, arguments, 5, accumulator);
}


static njs_int_t
njs_array_prototype_reduce(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t        i, length;
    njs_int_t       ret;
    njs_value_t     accumulator, *array, *value;
    njs_function_t  *function;

    if (nargs < 2 || !njs_is_array(&args[0]) || !njs_is_function(&args[1])) {
        njs_type_error(vm, "unexpected iterator arguments");
        return NJS_ERROR;
    }

    array = &args[0];
    length = njs_array_len(array);
    function =  njs_function(&args[1]);

    njs_set_invalid(&accumulator);

    if (nargs > 2) {
        accumulator = args[2];
    }

    for (i = 0; i < length; i++) {
        value = &njs_array_start(array)[i];

        if (njs_is_valid(value)) {

            if (!njs_is_valid(&accumulator)) {
                accumulator = njs_array_start(array)[i];
                continue;
            }

            ret = njs_array_iterator_reduce(vm, function, &accumulator, value,
                                            i, array);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        length = njs_min(length, njs_array_len(array));
    }

    if (!njs_is_valid(&accumulator)) {
        njs_type_error(vm, "invalid index");
        return NJS_ERROR;
    }

    vm->retval = accumulator;

    return NJS_OK;
}


static njs_int_t
njs_array_prototype_reduce_right(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    int32_t         i;
    uint32_t        length;
    njs_int_t       ret;
    njs_value_t     accumulator, *array, *value;
    njs_function_t  *function;

    if (nargs < 2 || !njs_is_array(&args[0]) || !njs_is_function(&args[1])) {
        njs_type_error(vm, "unexpected iterator arguments");
        return NJS_ERROR;
    }

    array = &args[0];
    length = njs_array_len(array);
    function =  njs_function(&args[1]);

    njs_set_invalid(&accumulator);

    if (nargs > 2) {
        accumulator = args[2];
    }

    for (i = length - 1; i >= 0; i--) {
        value = &njs_array_start(array)[i];

        if (njs_is_valid(value)) {

            if (!njs_is_valid(&accumulator)) {
                accumulator = njs_array_start(array)[i];
                continue;
            }

            ret = njs_array_iterator_reduce(vm, function, &accumulator, value,
                                            i, array);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        length = njs_min(length, njs_array_len(array));
    }

    if (!njs_is_valid(&accumulator)) {
        njs_type_error(vm, "invalid index");
        return NJS_ERROR;
    }

    vm->retval = accumulator;

    return NJS_OK;
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
    .args_types = { NJS_SKIP_ARG, NJS_STRING_ARG, NJS_STRING_ARG },
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
                        arguments[0] = njs_value_undefined;

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
        .type = NJS_METHOD,
        .name = njs_string("slice"),
        .value = njs_native_function(njs_array_prototype_slice,
                     NJS_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("push"),
        .value = njs_native_function(njs_array_prototype_push, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("pop"),
        .value = njs_native_function(njs_array_prototype_pop, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("unshift"),
        .value = njs_native_function(njs_array_prototype_unshift, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("shift"),
        .value = njs_native_function(njs_array_prototype_shift, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("splice"),
        .value = njs_native_function(njs_array_prototype_splice,
                    NJS_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("reverse"),
        .value = njs_native_function(njs_array_prototype_reverse,
                                     NJS_OBJECT_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_array_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("join"),
        .value = njs_native_function(njs_array_prototype_join,
                     NJS_OBJECT_ARG, NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("concat"),
        .value = njs_native_function(njs_array_prototype_concat, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("indexOf"),
        .value = njs_native_function(njs_array_prototype_index_of,
                     NJS_OBJECT_ARG, NJS_SKIP_ARG, NJS_INTEGER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("lastIndexOf"),
        .value = njs_native_function(njs_array_prototype_last_index_of,
                     NJS_OBJECT_ARG, NJS_SKIP_ARG, NJS_INTEGER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES7. */
    {
        .type = NJS_METHOD,
        .name = njs_string("includes"),
        .value = njs_native_function(njs_array_prototype_includes,
                     NJS_OBJECT_ARG, NJS_SKIP_ARG, NJS_INTEGER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("forEach"),
        .value = njs_native_function(njs_array_prototype_for_each, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("some"),
        .value = njs_native_function(njs_array_prototype_some, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("every"),
        .value = njs_native_function(njs_array_prototype_every, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("fill"),
        .value = njs_native_function(njs_array_prototype_fill,
                     NJS_OBJECT_ARG, NJS_SKIP_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("filter"),
        .value = njs_native_function(njs_array_prototype_filter, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("find"),
        .value = njs_native_function(njs_array_prototype_find, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("findIndex"),
        .value = njs_native_function(njs_array_prototype_find_index, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("map"),
        .value = njs_native_function(njs_array_prototype_map, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("reduce"),
        .value = njs_native_function(njs_array_prototype_reduce, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("reduceRight"),
        .value = njs_native_function(njs_array_prototype_reduce_right, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("sort"),
        .value = njs_native_function(njs_array_prototype_sort, 0),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_array_prototype_init = {
    njs_str("Array"),
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
    njs_str("Array instance"),
    njs_array_instance_properties,
    njs_nitems(njs_array_instance_properties),
};
