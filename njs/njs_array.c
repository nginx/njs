
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_stub.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_object_hash.h>
#include <njs_array.h>
#include <njs_function.h>
#include <string.h>


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
    njs_value_t             retval;
    int32_t                 index;
    uint32_t                length;
} njs_array_next_t;


static njs_ret_t
njs_array_prototype_to_string_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t retval);
static njs_ret_t njs_array_prototype_join_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static nxt_noinline njs_value_t *njs_array_copy(njs_value_t *dst,
    njs_value_t *src);
static nxt_noinline njs_ret_t njs_array_prototype_for_each_cont(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static nxt_noinline njs_ret_t njs_array_prototype_some_cont(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static nxt_noinline njs_ret_t njs_array_prototype_every_cont(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static nxt_noinline njs_ret_t njs_array_iterator_args(njs_vm_t *vm,
    njs_value_t * args, nxt_uint_t nargs);
static nxt_noinline nxt_int_t njs_array_iterator_next(njs_value_t *value,
    nxt_uint_t n, nxt_uint_t length);
static nxt_noinline njs_ret_t njs_array_iterator_apply(njs_vm_t *vm,
    njs_array_next_t *next, njs_value_t *args, nxt_uint_t nargs);


njs_value_t *
njs_array_add(njs_vm_t *vm, njs_value_t *value, u_char *start, size_t size)
{
    njs_ret_t    ret;
    njs_array_t  *array;

    if (value != NULL) {
        array = value->data.u.array;

        if (array->size == array->length) {
            ret = njs_array_realloc(vm, array, 0, array->size + 1);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NULL;
            }
        }

    } else {
        value = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    sizeof(njs_value_t));

        if (nxt_slow_path(value == NULL)) {
            return NULL;
        }

        array = njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
        if (nxt_slow_path(array == NULL)) {
            return NULL;
        }

        value->data.u.array = array;
        value->type = NJS_ARRAY;
        value->data.truth = 1;
    }

    ret = njs_string_create(vm, &array->start[array->length++], start, size, 0);

    if (nxt_fast_path(ret == NXT_OK)) {
        return value;
    }

    return NULL;
}


nxt_noinline njs_array_t *
njs_array_alloc(njs_vm_t *vm, uint32_t length, uint32_t spare)
{
    uint32_t     size;
    njs_array_t  *array;

    array = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_array_t));
    if (nxt_slow_path(array == NULL)) {
        return NULL;
    }

    size = length + spare;

    array->data = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                      size * sizeof(njs_value_t));
    if (nxt_slow_path(array->data == NULL)) {
        return NULL;
    }

    array->start = array->data;
    nxt_lvlhsh_init(&array->object.hash);
    nxt_lvlhsh_init(&array->object.shared_hash);
    array->object.__proto__ = &vm->prototypes[NJS_PROTOTYPE_ARRAY];
    array->object.shared = 0;
    array->size = size;
    array->length = length;

    return array;
}


njs_ret_t
njs_array_realloc(njs_vm_t *vm, njs_array_t *array, uint32_t prepend,
    uint32_t size)
{
    nxt_uint_t   n;
    njs_value_t  *value, *old;

    if (size != array->size) {
        if (size < 16) {
            size *= 2;

        } else {
            size += size / 2;
        }
    }

    value = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                (prepend + size) * sizeof(njs_value_t));
    if (nxt_slow_path(value == NULL)) {
        return NXT_ERROR;
    }

    old = array->data;
    array->data = value;

    while (prepend != 0) {
        njs_set_invalid(value);
        value++;
        prepend--;
    }

    memcpy(value, array->start, array->size * sizeof(njs_value_t));

    array->start = value;
    n = array->size;
    array->size = size;

    value += n;
    size -= n;

    while (size != 0) {
        njs_set_invalid(value);
        value++;
        size--;
    }

    nxt_mem_cache_free(vm->mem_cache_pool, old);

    return NXT_OK;
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
            vm->exception = &njs_exception_range_error;
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
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_array_constructor_init = {
    njs_array_constructor_properties,
    nxt_nitems(njs_array_constructor_properties),
};


static njs_ret_t
njs_array_prototype_length(njs_vm_t *vm, njs_value_t *array)
{
    njs_number_set(&vm->retval, array->data.u.array->length);

    njs_release(vm, array);

    return NXT_OK;
}


/*
 * Array.slice(start[, end]).
 * JavaScript 1.2, ECMAScript 3.
 */

static njs_ret_t
njs_array_prototype_slice(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    int32_t      start, end, length;
    uint32_t     n;
    njs_array_t  *array;
    njs_value_t  *value;

    start = 0;
    length = 0;

    if (njs_is_array(&args[0])) {
        length = args[0].data.u.array->length;

        if (nargs > 1) {
            start = args[1].data.u.number;

            if (start < 0) {
                start += length;

                if (start < 0) {
                    start = 0;
                }
            }

            end = length;

            if (nargs > 2) {
                end = args[2].data.u.number;

                if (end < 0) {
                    end += length;
                }
            }

            length = end - start;

            if (length < 0) {
                start = 0;
                length = 0;
            }
        }
    }

    array = njs_array_alloc(vm, length, NJS_ARRAY_SPARE);
    if (nxt_slow_path(array == NULL)) {
        return NXT_ERROR;
    }

    vm->retval.data.u.array = array;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    if (length != 0) {
        value = args[0].data.u.array->start;
        n = 0;

        do {
            /* GC: retain long string and object in values[start]. */
            array->start[n++] = value[start++];
            length--;
        } while (length != 0);
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
            if (nargs > array->size - array->length) {
                ret = njs_array_realloc(vm, array, 0, array->size + nargs);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }
            }

            for (i = 1; i < nargs; i++) {
                /* GC: njs_retain(&args[i]); */
                array->start[array->length++] = args[i];
            }
        }

        njs_number_set(&vm->retval, array->length);
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
                ret = njs_array_realloc(vm, array, n, array->size);
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

        njs_number_set(&vm->retval, array->length);
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

    cont = (njs_continuation_t *) njs_continuation(vm->frame);
    cont->function = njs_array_prototype_to_string_continuation;

    if (njs_is_object(&args[0])) {
        lhq.key_hash = NJS_JOIN_HASH;
        lhq.key.len = sizeof("join") - 1;
        lhq.key.data = (u_char *) "join";

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
    vm->frame->skip = 1;

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

    join = (njs_array_join_t *) njs_continuation(vm->frame);
    join->values = NULL;
    join->max = 0;
    max = 0;

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];
        if (njs_is_valid(value) && !njs_is_string(value)) {
            max++;
        }
    }

    if (max != 0) {
        values = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                     sizeof(njs_value_t) * max);
        if (nxt_slow_path(values == NULL)) {
            return NXT_ERROR;
        }

        join = (njs_array_join_t *) njs_continuation(vm->frame);
        join->cont.function = njs_array_prototype_join_continuation;
        join->values = values;
        join->max = max;

        n = 0;

        for (i = 0; i < array->length; i++) {
            value = &array->start[i];
            if (njs_is_valid(value) && !njs_is_string(value)) {
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

    join = (njs_array_join_t *) njs_continuation(vm->frame);
    values = join->values;
    max = join->max;

    size = 0;
    length = 0;
    n = 0;
    mask = -1;

    array = args[0].data.u.array;

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];

        if (njs_is_valid(value)) {

            if (!njs_is_string(value)) {
                value = &values[n++];

                if (!njs_is_string(value)) {
                    vm->frame->trap_scratch.data.u.value = value;

                    return NJS_TRAP_STRING_ARG;
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

        if (njs_is_valid(value)) {
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


static nxt_noinline njs_value_t *
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
njs_array_prototype_for_each(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t         ret;
    njs_array_next_t  *next;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    next = njs_continuation(vm->frame);
    next->u.cont.function = njs_array_prototype_for_each_cont;

    return njs_array_prototype_for_each_cont(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_for_each_cont(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_array_next_t  *next;

    next = njs_continuation(vm->frame);

    if (next->index < 0) {
        vm->retval = njs_value_void;
        return NXT_OK;
    }

    return njs_array_iterator_apply(vm, next, args, nargs);
}


static njs_ret_t
njs_array_prototype_some(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t         ret;
    njs_array_next_t  *next;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    next = njs_continuation(vm->frame);
    next->u.cont.function = njs_array_prototype_some_cont;
    next->retval.data.truth = 0;

    return njs_array_prototype_some_cont(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_some_cont(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_array_next_t   *next;
    const njs_value_t  *retval;

    next = njs_continuation(vm->frame);

    if (njs_is_true(&next->retval)) {
        retval = &njs_value_true;

    } else if (next->index < 0) {
        retval = &njs_value_false;

    } else {
        return njs_array_iterator_apply(vm, next, args, nargs);
    }

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_every(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t         ret;
    njs_array_next_t  *next;

    ret = njs_array_iterator_args(vm, args, nargs);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    next = njs_continuation(vm->frame);
    next->u.cont.function = njs_array_prototype_every_cont;
    next->retval.data.truth = 1;

    return njs_array_prototype_every_cont(vm, args, nargs, unused);
}


static njs_ret_t
njs_array_prototype_every_cont(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_array_next_t   *next;
    const njs_value_t  *retval;

    next = njs_continuation(vm->frame);

    if (!njs_is_true(&next->retval)) {
        retval = &njs_value_false;

    } else if (next->index < 0) {
        retval = &njs_value_true;

    } else {
        return njs_array_iterator_apply(vm, next, args, nargs);
    }

    vm->retval = *retval;

    return NXT_OK;
}


static nxt_noinline njs_ret_t
njs_array_iterator_args(njs_vm_t *vm, njs_value_t * args, nxt_uint_t nargs)
{
    njs_array_t       *array;
    njs_array_next_t  *next;

    if (nargs > 1 && njs_is_array(&args[0]) && njs_is_function(&args[1])) {

        array = args[0].data.u.array;
        next = njs_continuation(vm->frame);
        next->length = array->length;
        next->index = njs_array_iterator_next(array->start, 0, array->length);

        return NXT_OK;
    }

    vm->exception = &njs_exception_type_error;

    return NXT_ERROR;
}


static nxt_noinline nxt_int_t
njs_array_iterator_next(njs_value_t *value, nxt_uint_t n, nxt_uint_t length)
{
    while (n < length) {
        if (njs_is_valid(&value[n])) {
            return n;
        }

        n++;
    }

    return -1;
}


static nxt_noinline njs_ret_t
njs_array_iterator_apply(njs_vm_t *vm, njs_array_next_t *next,
    njs_value_t *args, nxt_uint_t nargs)
{
    nxt_int_t    n;
    njs_array_t  *array;
    njs_value_t  arguments[4];

    /*
     * The cast "*(njs_value_t *) &" is required by SunC.
     * Simple "(njs_value_t)" does not help.
     */
    arguments[0] = (nargs > 2) ? args[2] : *(njs_value_t *) &njs_value_void;
    /* GC: array elt, array */
    array = args[0].data.u.array;
    n = next->index;
    arguments[1] = array->start[n];
    njs_number_set(&arguments[2], n);
    arguments[3] = args[0];

    next->index = njs_array_iterator_next(array->start, ++n, next->length);

    return njs_function_apply(vm, args[1].data.u.function, arguments, 4,
                              (njs_index_t) &next->retval);
}


static const njs_object_prop_t  njs_array_prototype_properties[] =
{
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("length"),
        .value = njs_native_getter(njs_array_prototype_length),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("slice"),
        .value = njs_native_function(njs_array_prototype_slice, 0,
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
        .name = njs_string("forEach"),
        .value = njs_native_function(njs_array_prototype_for_each,
                     njs_continuation_size(njs_array_next_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("some"),
        .value = njs_native_function(njs_array_prototype_some,
                     njs_continuation_size(njs_array_next_t), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("every"),
        .value = njs_native_function(njs_array_prototype_every,
                     njs_continuation_size(njs_array_next_t), 0),
    },
};


const njs_object_init_t  njs_array_prototype_init = {
    njs_array_prototype_properties,
    nxt_nitems(njs_array_prototype_properties),
};
