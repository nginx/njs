
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>
#include <stdint.h>


typedef struct {
    union {
        njs_continuation_t  cont;
        u_char              padding[NJS_CONTINUATION_SIZE];
    } u;
    /*
     * This retval value must be aligned so the continuation is padded
     * to aligned size.
     */
    njs_value_t             length;
} njs_array_slice_t;


typedef struct {
    njs_continuation_t      cont;
    njs_value_t             *values;
    uint32_t                max;
} njs_array_join_t;


typedef struct {
    union {
        njs_continuation_t  cont;
        u_char              padding[NJS_CONTINUATION_SIZE];
    } u;
    /*
     * This retval value must be aligned so the continuation is padded
     * to aligned size.
     */
    njs_value_t             retval;

    uint32_t                index;
    uint32_t                length;
} njs_array_iter_t;


typedef struct {
    njs_array_iter_t        iter;
    njs_value_t             value;
    njs_array_t             *array;
} njs_array_filter_t;


typedef struct {
    njs_array_iter_t        iter;
    njs_value_t             value;
} njs_array_find_t;


typedef struct {
    njs_array_iter_t        iter;
    njs_array_t             *array;
} njs_array_map_t;


typedef struct {
    union {
        njs_continuation_t  cont;
        u_char              padding[NJS_CONTINUATION_SIZE];
    } u;
    /*
     * This retval value must be aligned so the continuation is padded
     * to aligned size.
     */
    njs_value_t             retval;

    njs_function_t          *function;
    uint32_t                index;
    uint32_t                current;
} njs_array_sort_t;


static njs_ret_t njs_array_prototype_slice_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_array_prototype_slice_copy(njs_vm_t *vm,
    njs_value_t *this, int64_t start, int64_t length);
static njs_ret_t njs_array_prototype_to_string_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t retval);
static njs_ret_t njs_array_prototype_join_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_value_t *njs_array_copy(njs_value_t *dst, njs_value_t *src);
static njs_ret_t njs_array_prototype_for_each_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_array_prototype_some_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_array_prototype_every_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_array_prototype_filter_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_array_prototype_find_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_array_prototype_find_index_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_array_prototype_map_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static nxt_noinline uint32_t njs_array_prototype_map_index(njs_array_t *array,
    njs_array_map_t *map);
static nxt_noinline njs_ret_t njs_array_iterator_args(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs);
static nxt_noinline uint32_t njs_array_iterator_index(njs_array_t *array,
    njs_array_iter_t *iter);
static nxt_noinline njs_ret_t njs_array_iterator_apply(njs_vm_t *vm,
    njs_array_iter_t *iter, njs_value_t *args, nxt_uint_t nargs);
static nxt_noinline njs_ret_t njs_array_prototype_find_apply(njs_vm_t *vm,
    njs_array_iter_t *iter, njs_value_t *args, nxt_uint_t nargs);
static njs_ret_t njs_array_prototype_reduce_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_array_prototype_reduce_right_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static uint32_t njs_array_reduce_right_index(njs_array_t *array,
    njs_array_iter_t *iter);
static njs_ret_t njs_array_prototype_sort_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);


nxt_noinline njs_array_t *
njs_array_alloc(njs_vm_t *vm, uint32_t length, uint32_t spare)
{
    uint64_t     size;
    njs_array_t  *array;

    array = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_array_t));
    if (nxt_slow_path(array == NULL)) {
        goto memory_error;
    }

    size = (uint64_t) length + spare;

    if (nxt_slow_path((size * sizeof(njs_value_t)) >= UINT32_MAX)) {
        goto memory_error;
    }

    array->data = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                      size * sizeof(njs_value_t));
    if (nxt_slow_path(array->data == NULL)) {
        goto memory_error;
    }

    array->start = array->data;
    nxt_lvlhsh_init(&array->object.hash);
    nxt_lvlhsh_init(&array->object.shared_hash);
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
}


njs_ret_t
njs_array_add(njs_vm_t *vm, njs_array_t *array, njs_value_t *value)
{
    njs_ret_t  ret;

    ret = njs_array_expand(vm, array, 0, 1);

    if (nxt_fast_path(ret == NXT_OK)) {
        /* GC: retain value. */
        array->start[array->length++] = *value;
    }

    return ret;
}


njs_ret_t
njs_array_string_add(njs_vm_t *vm, njs_array_t *array, u_char *start,
    size_t size, size_t length)
{
    njs_ret_t  ret;

    ret = njs_array_expand(vm, array, 0, 1);

    if (nxt_fast_path(ret == NXT_OK)) {
        return njs_string_create(vm, &array->start[array->length++],
                                 start, size, length);
    }

    return ret;
}


njs_ret_t
njs_array_expand(njs_vm_t *vm, njs_array_t *array, uint32_t prepend,
    uint32_t new_size)
{
    uint64_t     size;
    njs_value_t  *start, *old;

    size = (uint64_t) new_size + array->length;

    if (nxt_fast_path(size <= array->size && prepend == 0)) {
        return NXT_OK;
    }

    if (size < 16) {
        size *= 2;

    } else {
        size += size / 2;
    }

    if (nxt_slow_path(((prepend + size) * sizeof(njs_value_t)) >= UINT32_MAX)) {
        goto memory_error;
    }

    start = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                (prepend + size) * sizeof(njs_value_t));
    if (nxt_slow_path(start == NULL)) {
        goto memory_error;
    }

    array->size = size;

    old = array->data;
    array->data = start;
    start += prepend;

    memcpy(start, array->start, array->length * sizeof(njs_value_t));

    array->start = start;

    nxt_mem_cache_free(vm->mem_cache_pool, old);

    return NXT_OK;

memory_error:

    njs_memory_error(vm);

    return NXT_ERROR;
}


njs_ret_t
njs_array_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double       num;
    uint32_t     size;
    njs_value_t  *value;
    njs_array_t  *array;

    args = &args[1];
    size = nargs - 1;

    if (size == 1 && njs_is_number(&args[0])) {
        num = args[0].data.u.number;
        size = (uint32_t) num;

        if ((double) size != num) {
            njs_range_error(vm, "Invalid array length");
            return NXT_ERROR;
        }

        args = NULL;
    }

    array = njs_array_alloc(vm, size, NJS_ARRAY_SPARE);

    if (nxt_fast_path(array != NULL)) {

        vm->retval.data.u.array = array;
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

        vm->retval.type = NJS_ARRAY;
        vm->retval.data.truth = 1;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static njs_ret_t
njs_array_is_array(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    const njs_value_t  *value;

    if (nargs > 1 && njs_is_array(&args[1])) {
        value = &njs_value_true;

    } else {
        value = &njs_value_false;
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_array_of(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
        uint32_t     length, i;
        njs_array_t  *array;

        length = nargs > 1 ? nargs - 1 : 0;

        array = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
        if (nxt_slow_path(array == NULL)) {
            return NXT_ERROR;
        }

        vm->retval.data.u.array = array;
        vm->retval.type = NJS_ARRAY;
        vm->retval.data.truth = 1;

        for (i = 0; i < length; i++) {
            array->start[i] = args[i + 1];
        }

        return NXT_OK;
}


static const njs_object_prop_t  njs_array_constructor_properties[] =
{
    /* Array.name == "Array". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Array"),
    },

    /* Array.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
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
        .value = njs_native_function(njs_array_is_array, 0, 0),
    },

    /* ES6. */
    /* Array.of(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("of"),
        .value = njs_native_function(njs_array_of, 0, 0),
    },
};


const njs_object_init_t  njs_array_constructor_init = {
    nxt_string("Array"),
    njs_array_constructor_properties,
    nxt_nitems(njs_array_constructor_properties),
};


static njs_ret_t
njs_array_prototype_length(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval)
{
    double       num;
    int64_t      size;
    uint32_t     length;
    njs_ret_t    ret;
    njs_value_t  *val;
    njs_array_t  *array;

    array = value->data.u.array;

    if (setval != NULL) {
        if (!njs_is_number(setval)) {
            njs_range_error(vm, "Invalid array length");
            return NJS_ERROR;
        }

        num = setval->data.u.number;
        length = (uint32_t) num;

        if ((double) length != num) {
            njs_range_error(vm, "Invalid array length");
            return NJS_ERROR;
        }

        size = (int64_t) length - array->length;

        if (size > 0) {
            ret = njs_array_expand(vm, array, 0, size);
            if (nxt_slow_path(ret != NXT_OK)) {
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
    }

    njs_value_number_set(retval, array->length);

    return NJS_OK;
}


/*
 * Array.slice(start[, end]).
 * JavaScript 1.2, ECMAScript 3.
 */

static njs_ret_t
njs_array_prototype_slice(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_ret_t          ret;
    njs_array_slice_t  *slice;

    static const njs_value_t  njs_string_length = njs_string("length");

    slice = njs_vm_continuation(vm);
    slice->u.cont.function = njs_array_prototype_slice_continuation;

    ret = njs_value_property(vm, &args[0], &njs_string_length, &slice->length);
    if (nxt_slow_path(ret == NXT_ERROR || ret == NJS_TRAP)) {
        return ret;
    }

    return njs_array_prototype_slice_continuation(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_slice_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    int64_t            start, end, length;
    njs_array_slice_t  *slice;

    slice = njs_vm_continuation(vm);

    if (nxt_slow_path(!njs_is_primitive(&slice->length))) {
        njs_vm_trap_value(vm, &slice->length);
        return njs_trap(vm, NJS_TRAP_NUMBER_ARG);
    }

    start = (int32_t) njs_primitive_value_to_integer(njs_arg(args, nargs, 1));
    length = njs_primitive_value_to_integer(&slice->length);

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
        if (!njs_is_void(njs_arg(args, nargs, 2))) {
            end = (int32_t) njs_primitive_value_to_integer(&args[2]);

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


static njs_ret_t
njs_array_prototype_slice_copy(njs_vm_t *vm, njs_value_t *this,
    int64_t start, int64_t length)
{
    size_t             size;
    u_char             *dst;
    uint32_t           n, len;
    njs_ret_t          ret;
    njs_array_t        *array;
    njs_value_t        *value, name;
    const u_char       *src, *end;
    njs_slice_prop_t   string_slice;
    njs_string_prop_t  string;

    array = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
    if (nxt_slow_path(array == NULL)) {
        return NXT_ERROR;
    }

    vm->retval.data.u.array = array;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    if (length != 0) {
        n = 0;

        if (nxt_fast_path(njs_is_array(this))) {
            value = this->data.u.array->start;

            do {
                /* GC: retain long string and object in values[start]. */
                array->start[n++] = value[start++];
                length--;
            } while (length != 0);

        } else if (njs_is_string(this) || this->type == NJS_OBJECT_STRING) {

            if (this->type == NJS_OBJECT_STRING) {
                this = &this->data.u.object_value->value;
            }

            string_slice.start = start;
            string_slice.length = length;
            string_slice.string_length = njs_string_prop(&string, this);

            njs_string_slice_string_prop(&string, &string, &string_slice);

            src = string.start;
            end = src + string.size;

            if (string.length == 0) {
                /* Byte string. */
                len = 0;

            } else {
                /* UTF-8 or ASCII string. */
                len = 1;
            }

            do {
                value = &array->start[n++];
                dst = njs_string_short_start(value);
                dst = nxt_utf8_copy(dst, &src, end);
                size = dst - njs_string_short_start(value);
                njs_string_short_set(value, size, len);

                length--;
            } while (length != 0);

        } else if (njs_is_object(this)) {

            do {
                njs_uint32_to_string(&name, start++);

                value = &array->start[n++];
                ret = njs_value_property(vm, this, &name, value);

                if (ret != NXT_OK) {
                    *value = njs_value_invalid;
                }

                length--;
            } while (length != 0);
        }
    }

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_push(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_ret_t    ret;
    nxt_uint_t   i;
    njs_array_t  *array;

    if (njs_is_array(&args[0])) {
        array = args[0].data.u.array;

        if (nargs != 0) {
            ret = njs_array_expand(vm, array, 0, nargs);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            for (i = 1; i < nargs; i++) {
                /* GC: njs_retain(&args[i]); */
                array->start[array->length++] = args[i];
            }
        }

        njs_value_number_set(&vm->retval, array->length);
    }

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_pop(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_array_t        *array;
    const njs_value_t  *retval, *value;

    retval = &njs_value_void;

    if (njs_is_array(&args[0])) {
        array = args[0].data.u.array;

        if (array->length != 0) {
            array->length--;
            value = &array->start[array->length];

            if (njs_is_valid(value)) {
                retval = value;
            }
        }
    }

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_unshift(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_ret_t    ret;
    nxt_uint_t   n;
    njs_array_t  *array;

    if (njs_is_array(&args[0])) {
        array = args[0].data.u.array;
        n = nargs - 1;

        if (n != 0) {
            if ((intptr_t) n > (array->start - array->data)) {
                ret = njs_array_expand(vm, array, n, 0);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }
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

        njs_value_number_set(&vm->retval, array->length);
    }

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_shift(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_array_t        *array;
    const njs_value_t  *retval, *value;

    retval = &njs_value_void;

    if (njs_is_array(&args[0])) {
        array = args[0].data.u.array;

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

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_splice(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_ret_t    ret;
    nxt_int_t    n, start, length, items, delta, delete;
    nxt_uint_t   i;
    njs_array_t  *array, *deleted;

    array = NULL;
    start = 0;
    delete = 0;

    if (njs_is_array(&args[0])) {
        array = args[0].data.u.array;
        length = array->length;

        if (nargs > 1) {
            start = args[1].data.u.number;

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
                n = args[2].data.u.number;

                if (n < 0) {
                    delete = 0;

                } else if (n < delete) {
                    delete = n;
                }
            }
        }
    }

    deleted = njs_array_alloc(vm, delete, 0);
    if (nxt_slow_path(deleted == NULL)) {
        return NXT_ERROR;
    }

    if (array != NULL && (delete >= 0 || nargs > 3)) {

        /* Move deleted items to a new array to return. */
        for (i = 0, n = start; i < (nxt_uint_t) delete; i++, n++) {
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
                if (nxt_slow_path(ret != NXT_OK)) {
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

    vm->retval.data.u.array = deleted;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_reverse(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_uint_t   i, n, length;
    njs_value_t  value;
    njs_array_t  *array;

    if (njs_is_array(&args[0])) {
        array = args[0].data.u.array;
        length = array->length;

        if (length > 1) {
            for (i = 0, n = length - 1; i < n; i++, n--) {
                value = array->start[i];
                array->start[i] = array->start[n];
                array->start[n] = value;
            }
        }

        vm->retval.data.u.array = array;
        vm->retval.type = NJS_ARRAY;
        vm->retval.data.truth = 1;

    } else {
        /* STUB */
        vm->retval = args[0];
    }

    return NXT_OK;
}


/*
 * ECMAScript 5.1: try first to use object method "join", then
 * use the standard built-in method Object.prototype.toString().
 * Array.toString() must be a continuation otherwise it may
 * endlessly call Array.join().
 */

static njs_ret_t
njs_array_prototype_to_string(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t retval)
{
    njs_object_prop_t   *prop;
    njs_continuation_t  *cont;
    nxt_lvlhsh_query_t  lhq;

    cont = njs_vm_continuation(vm);
    cont->function = njs_array_prototype_to_string_continuation;

    if (njs_is_object(&args[0])) {
        lhq.key_hash = NJS_JOIN_HASH;
        lhq.key = nxt_string_value("join");

        prop = njs_object_property(vm, args[0].data.u.object, &lhq);

        if (nxt_fast_path(prop != NULL && njs_is_function(&prop->value))) {
            return njs_function_apply(vm, prop->value.data.u.function,
                                      args, nargs, retval);
        }
    }

    return njs_object_prototype_to_string(vm, args, nargs, retval);
}


static njs_ret_t
njs_array_prototype_to_string_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t retval)
{
    /* Skip retval update. */
    vm->top_frame->skip = 1;

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_join(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    uint32_t          max;
    nxt_uint_t        i, n;
    njs_array_t       *array;
    njs_value_t       *value, *values;
    njs_array_join_t  *join;

    if (!njs_is_array(&args[0])) {
        goto empty;
    }

    array = args[0].data.u.array;

    if (array->length == 0) {
        goto empty;
    }

    join = njs_vm_continuation(vm);
    join->values = NULL;
    join->max = 0;
    max = 0;

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];

        if (!njs_is_string(value)
            && njs_is_valid(value)
            && !njs_is_null_or_void(value))
        {
            max++;
        }
    }

    if (max != 0) {
        values = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                     sizeof(njs_value_t) * max);
        if (nxt_slow_path(values == NULL)) {
            njs_memory_error(vm);
            return NXT_ERROR;
        }

        join = njs_vm_continuation(vm);
        join->cont.function = njs_array_prototype_join_continuation;
        join->values = values;
        join->max = max;

        n = 0;

        for (i = 0; i < array->length; i++) {
            value = &array->start[i];

            if (!njs_is_string(value)
                && njs_is_valid(value)
                && !njs_is_null_or_void(value))
            {
                values[n++] = *value;

                if (n >= max) {
                    break;
                }
            }
        }
    }

    return njs_array_prototype_join_continuation(vm, args, nargs, unused);

empty:

    vm->retval = njs_string_empty;

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_join_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    u_char             *p;
    size_t             size, length, mask;
    uint32_t           max;
    nxt_uint_t         i, n;
    njs_array_t        *array;
    njs_value_t        *value, *values;
    njs_array_join_t   *join;
    njs_string_prop_t  separator, string;

    join = njs_vm_continuation(vm);
    values = join->values;
    max = join->max;

    size = 0;
    length = 0;
    n = 0;
    mask = -1;

    array = args[0].data.u.array;

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];

        if (njs_is_valid(value) && !njs_is_null_or_void(value)) {

            if (!njs_is_string(value)) {
                value = &values[n++];

                if (!njs_is_string(value)) {
                    njs_vm_trap_value(vm, value);

                    return njs_trap(vm, NJS_TRAP_STRING_ARG);
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
        value = (njs_value_t *) &njs_string_comma;
    }

    (void) njs_string_prop(&separator, value);

    size += separator.size * (array->length - 1);
    length += separator.length * (array->length - 1);

    length &= mask;

    p = njs_string_alloc(vm, &vm->retval, size, length);
    if (nxt_slow_path(p == NULL)) {
        return NXT_ERROR;
    }

    n = 0;

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];

        if (njs_is_valid(value) && !njs_is_null_or_void(value)) {
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

    nxt_mem_cache_free(vm->mem_cache_pool, values);

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_concat(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    size_t       length;
    nxt_uint_t   i;
    njs_value_t  *value;
    njs_array_t  *array;

    length = 0;

    for (i = 0; i < nargs; i++) {
        if (njs_is_array(&args[i])) {
            length += args[i].data.u.array->length;

        } else {
            length++;
        }
    }

    array = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
    if (nxt_slow_path(array == NULL)) {
        return NXT_ERROR;
    }

    vm->retval.data.u.array = array;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    value = array->start;

    for (i = 0; i < nargs; i++) {
        value = njs_array_copy(value, &args[i]);
    }

    return NXT_OK;
}


static njs_value_t *
njs_array_copy(njs_value_t *dst, njs_value_t *src)
{
    nxt_uint_t  n;

    n = 1;

    if (njs_is_array(src)) {
        n = src->data.u.array->length;
        src = src->data.u.array->start;
    }

    while (n != 0) {
        /* GC: njs_retain src */
        *dst++ = *src++;
        n--;
    }

    return dst;
}


static njs_ret_t
njs_array_prototype_index_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t    i, index, length;
    njs_value_t  *value, *start;
    njs_array_t  *array;

    index = -1;

    if (nargs < 2 || !njs_is_array(&args[0])) {
        goto done;
    }

    array = args[0].data.u.array;
    length = array->length;

    if (length == 0) {
        goto done;
    }

    i = 0;

    if (nargs > 2) {
        i = args[2].data.u.number;

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

    njs_value_number_set(&vm->retval, index);

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_last_index_of(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    nxt_int_t    i, n, index, length;
    njs_value_t  *value, *start;
    njs_array_t  *array;

    index = -1;

    if (nargs < 2 || !njs_is_array(&args[0])) {
        goto done;
    }

    array = args[0].data.u.array;
    length = array->length;

    if (length == 0) {
        goto done;
    }

    i = length - 1;

    if (nargs > 2) {
        n = args[2].data.u.number;

        if (n < 0) {
            i = n + length;

            if (i < 0) {
                goto done;
            }

        } else if (n < length) {
            i = n;
        }
    }

    value = &args[1];
    start = array->start;

    do {
        if (njs_values_strict_equal(value, &start[i])) {
            index = i;
            break;
        }

        i--;

    } while (i >= 0);

done:

    njs_value_number_set(&vm->retval, index);

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_includes(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t          i, length;
    njs_value_t        *value, *start;
    njs_array_t        *array;
    const njs_value_t  *retval;

    retval = &njs_value_false;

    if (nargs < 2 || !njs_is_array(&args[0])) {
        goto done;
    }

    array = args[0].data.u.array;
    length = array->length;

    if (length == 0) {
        goto done;
    }

    i = 0;

    if (nargs > 2) {
        i = args[2].data.u.number;

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

    if (njs_is_number(value) && isnan(value->data.u.number)) {

        do {
            value = &start[i];

            if (njs_is_number(value) && isnan(value->data.u.number)) {
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

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_fill(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t    i, start, end, length;
    njs_array_t  *array;

    vm->retval = args[0];

    if (nargs < 2 || !njs_is_array(&args[0])) {
        return NXT_OK;
    }

    array = args[0].data.u.array;
    length = array->length;

    if (length == 0) {
        return NXT_OK;
    }

    start = 0;
    end = length;

    if (nargs > 2) {
        start = args[2].data.u.number;

        if (start > length) {
            start = length;
        }

        if (start < 0) {
            start += length;

            if (start < 0) {
                start = 0;
            }
        }

       if (nargs > 3) {
           end = args[3].data.u.number;

           if (end > length) {
               end = length;
           }

           if (end < 0) {
               end += length;

               if (end < 0) {
                   end = 0;
               }
           }
       }
    }

    for (i = start; i < end; i++) {
        array->start[i] = args[1];
    }

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_for_each(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t         ret;
    njs_array_iter_t  *iter;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    iter = njs_vm_continuation(vm);
    iter->u.cont.function = njs_array_prototype_for_each_continuation;

    return njs_array_prototype_for_each_continuation(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_for_each_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t          index;
    njs_array_iter_t  *iter;

    iter = njs_vm_continuation(vm);

    index = njs_array_iterator_index(args[0].data.u.array, iter);

    if (index == NJS_ARRAY_INVALID_INDEX) {
        vm->retval = njs_value_void;
        return NXT_OK;
    }

    return njs_array_iterator_apply(vm, iter, args, nargs);
}


static njs_ret_t
njs_array_prototype_some(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t         ret;
    njs_array_iter_t  *iter;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    iter = njs_vm_continuation(vm);
    iter->u.cont.function = njs_array_prototype_some_continuation;

    return njs_array_prototype_some_continuation(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_some_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t           index;
    njs_array_iter_t   *iter;
    const njs_value_t  *retval;

    iter = njs_vm_continuation(vm);

    if (njs_is_true(&iter->retval)) {
        retval = &njs_value_true;

    } else {
        index = njs_array_iterator_index(args[0].data.u.array, iter);

        if (index == NJS_ARRAY_INVALID_INDEX) {
            retval = &njs_value_false;

        } else {
            return njs_array_iterator_apply(vm, iter, args, nargs);
        }
    }

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_every(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t         ret;
    njs_array_iter_t  *iter;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    iter = njs_vm_continuation(vm);
    iter->u.cont.function = njs_array_prototype_every_continuation;
    iter->retval.data.truth = 1;

    return njs_array_prototype_every_continuation(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_every_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t           index;
    njs_array_iter_t   *iter;
    const njs_value_t  *retval;

    iter = njs_vm_continuation(vm);

    if (!njs_is_true(&iter->retval)) {
        retval = &njs_value_false;

    } else {
        index = njs_array_iterator_index(args[0].data.u.array, iter);

        if (index == NJS_ARRAY_INVALID_INDEX) {
            retval = &njs_value_true;

        } else {
            return njs_array_iterator_apply(vm, iter, args, nargs);
        }
    }

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_filter(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t           ret;
    njs_array_filter_t  *filter;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    filter = njs_vm_continuation(vm);
    filter->iter.u.cont.function = njs_array_prototype_filter_continuation;

    filter->array = njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
    if (nxt_slow_path(filter->array == NULL)) {
        return NXT_ERROR;
    }

    return njs_array_prototype_filter_continuation(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_filter_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t            index;
    nxt_int_t           ret;
    njs_array_t         *array;
    njs_array_filter_t  *filter;

    filter = njs_vm_continuation(vm);

    if (njs_is_true(&filter->iter.retval)) {
        ret = njs_array_add(vm, filter->array, &filter->value);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    array = args[0].data.u.array;
    index = njs_array_iterator_index(array, &filter->iter);

    if (index == NJS_ARRAY_INVALID_INDEX) {
        vm->retval.data.u.array = filter->array;
        vm->retval.type = NJS_ARRAY;
        vm->retval.data.truth = 1;

        return NXT_OK;
    }

    /* GC: filter->value */
    filter->value = array->start[index];

    return njs_array_iterator_apply(vm, &filter->iter, args, nargs);
}


static njs_ret_t
njs_array_prototype_find(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t         ret;
    njs_array_find_t  *find;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    find = njs_vm_continuation(vm);
    find->iter.u.cont.function = njs_array_prototype_find_continuation;

    return njs_array_prototype_find_continuation(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_find_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_array_t        *array;
    njs_array_iter_t   *iter;
    njs_array_find_t   *find;
    const njs_value_t  *retval;

    retval = &njs_value_void;

    find = njs_vm_continuation(vm);
    iter = &find->iter;

    if (!njs_is_true(&iter->retval)) {
        array = args[0].data.u.array;
        iter->index++;

        if (iter->index < iter->length && iter->index < array->length) {
            /* GC: find->value */
            find->value = array->start[iter->index];

            return njs_array_prototype_find_apply(vm, iter, args, nargs);
        }

    } else {
        if (njs_is_valid(&find->value)) {
            retval = &find->value;
        }
    }

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_find_index(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    nxt_int_t         ret;
    njs_array_iter_t  *iter;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    iter = njs_vm_continuation(vm);
    iter->u.cont.function = njs_array_prototype_find_index_continuation;

    return njs_array_prototype_find_index_continuation(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_find_index_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double             index;
    njs_array_iter_t   *iter;

    iter = njs_vm_continuation(vm);
    index = iter->index;

    if (!njs_is_true(&iter->retval)) {
        iter->index++;

        if (iter->index < iter->length
            && iter->index < args[0].data.u.array->length)
        {
            return njs_array_prototype_find_apply(vm, iter, args, nargs);
        }

        index = -1;
    }

    njs_value_number_set(&vm->retval, index);

    return NXT_OK;
}


static nxt_noinline njs_ret_t
njs_array_prototype_find_apply(njs_vm_t *vm, njs_array_iter_t *iter,
    njs_value_t *args, nxt_uint_t nargs)
{
    uint32_t           n;
    const njs_value_t  *value;
    njs_value_t        arguments[4];

    /* GC: array elt, array */

    value = njs_arg(args, nargs, 2);
    arguments[0] = *value;

    n = iter->index;
    value = &args[0].data.u.array->start[n];

    if (!njs_is_valid(value)) {
        value = &njs_value_void;
    }

    arguments[1] = *value;

    njs_value_number_set(&arguments[2], n);

    arguments[3] = args[0];

    return njs_function_apply(vm, args[1].data.u.function, arguments, 4,
                              (njs_index_t) &iter->retval);
}


static njs_ret_t
njs_array_prototype_map(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t        ret;
    njs_array_map_t  *map;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    map = njs_vm_continuation(vm);
    map->iter.u.cont.function = njs_array_prototype_map_continuation;
    njs_set_invalid(&map->iter.retval);

    map->array = njs_array_alloc(vm, args[0].data.u.array->length, 0);
    if (nxt_slow_path(map->array == NULL)) {
        return NXT_ERROR;
    }

    return njs_array_prototype_map_continuation(vm, args, nargs, unused);
}


static nxt_noinline njs_ret_t
njs_array_prototype_map_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t         index;
    njs_array_map_t  *map;

    map = njs_vm_continuation(vm);

    if (njs_is_valid(&map->iter.retval)) {
        map->array->start[map->iter.index] = map->iter.retval;
    }

    index = njs_array_prototype_map_index(args[0].data.u.array, map);

    if (index == NJS_ARRAY_INVALID_INDEX) {
        vm->retval.data.u.array = map->array;
        vm->retval.type = NJS_ARRAY;
        vm->retval.data.truth = 1;

        return NXT_OK;
    }

    return njs_array_iterator_apply(vm, &map->iter, args, nargs);
}


static uint32_t
njs_array_prototype_map_index(njs_array_t *array, njs_array_map_t *map)
{
    uint32_t     i, length;
    njs_value_t  *start;

    start = map->array->start;
    length = nxt_min(array->length, map->iter.length);

    for (i = map->iter.index + 1; i < length; i++) {
        if (njs_is_valid(&array->start[i])) {
            map->iter.index = i;
            return i;
        }

        njs_set_invalid(&start[i]);
    }

    while (i < map->iter.length) {
        njs_set_invalid(&start[i++]);
    }

    return NJS_ARRAY_INVALID_INDEX;
}


static njs_ret_t
njs_array_prototype_reduce(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    uint32_t          n;
    nxt_int_t         ret;
    njs_array_t       *array;
    njs_array_iter_t  *iter;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    iter = njs_vm_continuation(vm);
    iter->u.cont.function = njs_array_prototype_reduce_continuation;

    if (nargs > 2) {
        iter->retval = args[2];

    } else {
        array = args[0].data.u.array;
        n = njs_array_iterator_index(array, iter);

        if (n == NJS_ARRAY_INVALID_INDEX) {
            njs_type_error(vm, "invalid index");
            return NXT_ERROR;
        }

        iter->retval = array->start[n];
    }

    return njs_array_prototype_reduce_continuation(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_reduce_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t          n;
    njs_array_t       *array;
    njs_value_t       arguments[5];
    njs_array_iter_t  *iter;

    iter = njs_vm_continuation(vm);
    array = args[0].data.u.array;

    n = njs_array_iterator_index(array, iter);

    if (n == NJS_ARRAY_INVALID_INDEX) {
        vm->retval = iter->retval;
        return NXT_OK;
    }

    arguments[0] = njs_value_void;

    /* GC: array elt, array */
    arguments[1] = iter->retval;

    arguments[2] = array->start[n];

    njs_value_number_set(&arguments[3], n);

    arguments[4] = args[0];

    return njs_function_apply(vm, args[1].data.u.function, arguments, 5,
                              (njs_index_t) &iter->retval);
}


static nxt_noinline njs_ret_t
njs_array_iterator_args(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs)
{
    njs_array_iter_t  *iter;

    if (nargs > 1 && njs_is_array(&args[0]) && njs_is_function(&args[1])) {

        iter = njs_vm_continuation(vm);
        iter->length = args[0].data.u.array->length;
        iter->retval.data.truth = 0;
        iter->index = NJS_ARRAY_INVALID_INDEX;

        return NXT_OK;
    }

    njs_type_error(vm, "unexpected iterator arguments");

    return NXT_ERROR;
}


static nxt_noinline uint32_t
njs_array_iterator_index(njs_array_t *array, njs_array_iter_t *iter)
{
    uint32_t  i, length;

    length = nxt_min(array->length, iter->length);

    for (i = iter->index + 1; i < length; i++) {
        if (njs_is_valid(&array->start[i])) {
            iter->index = i;
            return i;
        }
    }

    return NJS_ARRAY_INVALID_INDEX;
}


static nxt_noinline njs_ret_t
njs_array_iterator_apply(njs_vm_t *vm, njs_array_iter_t *iter,
    njs_value_t *args, nxt_uint_t nargs)
{
    uint32_t           n;
    const njs_value_t  *value;
    njs_value_t        arguments[4];

    /* GC: array elt, array */

    value = njs_arg(args, nargs, 2);
    arguments[0] = *value;

    n = iter->index;
    arguments[1] = args[0].data.u.array->start[n];

    njs_value_number_set(&arguments[2], n);

    arguments[3] = args[0];

    return njs_function_apply(vm, args[1].data.u.function, arguments, 4,
                              (njs_index_t) &iter->retval);
}


static njs_ret_t
njs_array_prototype_reduce_right(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t          n;
    njs_ret_t         ret;
    njs_array_t       *array;
    njs_array_iter_t  *iter;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    iter = njs_vm_continuation(vm);
    iter->u.cont.function = njs_array_prototype_reduce_right_continuation;

    if (nargs > 2) {
        iter->retval = args[2];

    } else {
        array = args[0].data.u.array;
        n = njs_array_reduce_right_index(array, iter);

        if (n == NJS_ARRAY_INVALID_INDEX) {
            njs_type_error(vm, "invalid index");

            return NXT_ERROR;
        }

        iter->retval = array->start[n];
    }

    return njs_array_prototype_reduce_right_continuation(vm, args, nargs,
                                                         unused);
}


static nxt_noinline njs_ret_t
njs_array_prototype_reduce_right_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t          n;
    njs_array_t       *array;
    njs_value_t       arguments[5];
    njs_array_iter_t  *iter;

    iter = njs_vm_continuation(vm);
    array = args[0].data.u.array;

    n = njs_array_reduce_right_index(array, iter);

    if (n == NJS_ARRAY_INVALID_INDEX) {
        vm->retval = iter->retval;
        return NXT_OK;
    }

    arguments[0] = njs_value_void;

    /* GC: array elt, array */
    arguments[1] = iter->retval;

    arguments[2] = array->start[n];

    njs_value_number_set(&arguments[3], n);

    arguments[4] = args[0];

    return njs_function_apply(vm, args[1].data.u.function, arguments, 5,
                              (njs_index_t) &iter->retval);
}


static nxt_noinline uint32_t
njs_array_reduce_right_index(njs_array_t *array, njs_array_iter_t *iter)
{
    uint32_t  n;

    n = nxt_min(iter->index, array->length) - 1;

    while (n != NJS_ARRAY_INVALID_INDEX) {

        if (njs_is_valid(&array->start[n])) {
            iter->index = n;
            break;
        }

        n--;
    }

    return n;
}


static njs_ret_t
njs_array_string_sort(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    nxt_int_t   ret;
    nxt_uint_t  i;

    for (i = 1; i < nargs; i++) {
        if (!njs_is_string(&args[i])) {
            njs_vm_trap_value(vm, &args[i]);

            return njs_trap(vm, NJS_TRAP_STRING_ARG);
        }
    }

    ret = njs_string_cmp(&args[1], &args[2]);

    njs_value_number_set(&vm->retval, ret);

    return NXT_OK;
}


static const njs_function_t  njs_array_string_sort_function = {
    .object = { .type = NJS_FUNCTION, .shared = 1, .extensible = 1 },
    .native = 1,
    .continuation_size = NJS_CONTINUATION_SIZE,
    .args_types = { NJS_SKIP_ARG, NJS_STRING_ARG, NJS_STRING_ARG },
    .args_offset = 1,
    .u.native = njs_array_string_sort,
};


static njs_ret_t
njs_array_prototype_sort(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_array_sort_t  *sort;

    if (njs_is_array(&args[0]) && args[0].data.u.array->length > 1) {

        sort = njs_vm_continuation(vm);
        sort->u.cont.function = njs_array_prototype_sort_continuation;
        sort->current = 0;
        sort->retval = njs_value_zero;

        if (nargs > 1 && njs_is_function(&args[1])) {
            sort->function = args[1].data.u.function;

        } else {
            sort->function = (njs_function_t *) &njs_array_string_sort_function;
        }

        return njs_array_prototype_sort_continuation(vm, args, nargs, unused);
    }

    vm->retval = args[0];

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_sort_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    uint32_t          n;
    njs_array_t       *array;
    njs_value_t       value, *start, arguments[3];
    njs_array_sort_t  *sort;

    array = args[0].data.u.array;
    start = array->start;

    sort = njs_vm_continuation(vm);

    if (njs_is_number(&sort->retval)) {

        /*
         * The sort function is implemented with the insertion sort algorithm.
         * Its worst and average computational complexity is O^2.  This point
         * should be considered as return point from comparison function so
         * "goto next" moves control to the appropriate step of the algorithm.
         * The first iteration also goes there because sort->retval is zero.
         */
        if (sort->retval.data.u.number <= 0) {
            goto next;
        }

        n = sort->index;

    swap:

        value = start[n];
        start[n] = start[n - 1];
        n--;
        start[n] = value;

        do {
            if (n > 0) {

                if (njs_is_valid(&start[n])) {

                    if (njs_is_valid(&start[n - 1])) {
                        arguments[0] = njs_value_void;

                        /* GC: array elt, array */
                        arguments[1] = start[n - 1];
                        arguments[2] = start[n];

                        sort->index = n;

                        return njs_function_apply(vm, sort->function,
                                                  arguments, 3,
                                                  (njs_index_t) &sort->retval);
                    }

                    /* Move invalid values to the end of array. */
                    goto swap;
                }
            }

        next:

            sort->current++;
            n = sort->current;

        } while (n < array->length);
    }

    vm->retval = args[0];

    return NXT_OK;
}


static const njs_object_prop_t  njs_array_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("length"),
        .value = njs_prop_handler(njs_array_prototype_length),
        .writable = 1
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("slice"),
        .value = njs_native_function(njs_array_prototype_slice,
                     njs_continuation_size(njs_array_slice_t),
                     NJS_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("push"),
        .value = njs_native_function(njs_array_prototype_push, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("pop"),
        .value = njs_native_function(njs_array_prototype_pop, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("unshift"),
        .value = njs_native_function(njs_array_prototype_unshift, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("shift"),
        .value = njs_native_function(njs_array_prototype_shift, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("splice"),
        .value = njs_native_function(njs_array_prototype_splice, 0,
                    NJS_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("reverse"),
        .value = njs_native_function(njs_array_prototype_reverse, 0,
                    NJS_OBJECT_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_array_prototype_to_string,
                     NJS_CONTINUATION_SIZE, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("join"),
        .value = njs_native_function(njs_array_prototype_join,
                     njs_continuation_size(njs_array_join_t),
                     NJS_OBJECT_ARG, NJS_STRING_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("concat"),
        .value = njs_native_function(njs_array_prototype_concat, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("indexOf"),
        .value = njs_native_function(njs_array_prototype_index_of, 0,
                     NJS_OBJECT_ARG, NJS_SKIP_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("lastIndexOf"),
        .value = njs_native_function(njs_array_prototype_last_index_of, 0,
                     NJS_OBJECT_ARG, NJS_SKIP_ARG, NJS_INTEGER_ARG),
    },

    /* ES7. */
    {
        .type = NJS_METHOD,
        .name = njs_string("includes"),
        .value = njs_native_function(njs_array_prototype_includes, 0,
                     NJS_OBJECT_ARG, NJS_SKIP_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("forEach"),
        .value = njs_native_function(njs_array_prototype_for_each,
                     njs_continuation_size(njs_array_iter_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("some"),
        .value = njs_native_function(njs_array_prototype_some,
                     njs_continuation_size(njs_array_iter_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("every"),
        .value = njs_native_function(njs_array_prototype_every,
                     njs_continuation_size(njs_array_iter_t), 0),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("fill"),
        .value = njs_native_function(njs_array_prototype_fill, 0,
                     NJS_OBJECT_ARG, NJS_SKIP_ARG, NJS_NUMBER_ARG,
                     NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("filter"),
        .value = njs_native_function(njs_array_prototype_filter,
                     njs_continuation_size(njs_array_filter_t), 0),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("find"),
        .value = njs_native_function(njs_array_prototype_find,
                     njs_continuation_size(njs_array_find_t), 0),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("findIndex"),
        .value = njs_native_function(njs_array_prototype_find_index,
                     njs_continuation_size(njs_array_iter_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("map"),
        .value = njs_native_function(njs_array_prototype_map,
                     njs_continuation_size(njs_array_map_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("reduce"),
        .value = njs_native_function(njs_array_prototype_reduce,
                     njs_continuation_size(njs_array_iter_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("reduceRight"),
        .value = njs_native_function(njs_array_prototype_reduce_right,
                     njs_continuation_size(njs_array_iter_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("sort"),
        .value = njs_native_function(njs_array_prototype_sort,
                     njs_continuation_size(njs_array_iter_t), 0),
    },
};


const njs_object_init_t  njs_array_prototype_init = {
    nxt_string("Array"),
    njs_array_prototype_properties,
    nxt_nitems(njs_array_prototype_properties),
};
