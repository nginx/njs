
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
    njs_value_t  retval;
    int32_t      index;
    uint32_t     length;
} njs_array_next_t;


static nxt_noinline njs_value_t *njs_array_copy(njs_value_t *dst,
    njs_value_t *src);
static nxt_noinline nxt_int_t njs_array_next(njs_value_t *value, nxt_uint_t n,
    nxt_uint_t length);


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
    array->size = size;
    array->length = length;

    return array;
}


njs_ret_t
njs_array_realloc(njs_vm_t *vm, njs_array_t *array, uint32_t prepend,
    uint32_t size)
{
    nxt_uint_t   n;
    njs_value_t  *value;

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

    /* GC: old = array->data */

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

    /* GC: free old pointer. */

    return NXT_OK;
}


njs_ret_t
njs_array_constructor(njs_vm_t *vm, njs_param_t *param)
{
    double       num;
    uint32_t     size;
    njs_value_t  *value, *args;
    njs_array_t  *array;

    args = param->args;
    size = param->nargs;

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
njs_array_prototype_slice(njs_vm_t *vm, njs_param_t *param)
{
    int32_t      start, end, length;
    uint32_t     n;
    uintptr_t    nargs;
    njs_array_t  *array;
    njs_value_t  *this, *value;

    start = 0;
    length = 0;
    this = param->this;

    if (njs_is_array(this)) {
        length = this->data.u.array->length;
        nargs = param->nargs;

        if (nargs != 0) {
            start = param->args[0].data.u.number;

            if (start < 0) {
                start += length;

                if (start < 0) {
                    start = 0;
                }
            }

            end = length;

            if (nargs > 1) {
                end = param->args[1].data.u.number;

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
        value = this->data.u.array->start;
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
njs_array_prototype_push(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t    i, nargs;
    njs_ret_t    ret;
    njs_value_t  *this, *args;
    njs_array_t  *array;

    this = param->this;

    if (njs_is_array(this)) {
        array = this->data.u.array;
        nargs = param->nargs;

        if (nargs != 0) {
            if (nargs > array->size - array->length) {
                ret = njs_array_realloc(vm, array, 0, array->size + nargs);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }
            }

            args = param->args;

            for (i = 0; i < nargs; i++) {
                /* GC: njs_retain(&args[i]); */
                array->start[array->length++] = args[i];
            }
        }

        njs_number_set(&vm->retval, array->length);
    }

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_pop(njs_vm_t *vm, njs_param_t *param)
{
    njs_array_t        *array;
    njs_value_t        *this;
    const njs_value_t  *retval, *value;

    retval = &njs_value_void;

    this = param->this;

    if (njs_is_array(this)) {
        array = this->data.u.array;

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
njs_array_prototype_unshift(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t    nargs;
    njs_ret_t    ret;
    njs_value_t  *this, *args;
    njs_array_t  *array;

    this = param->this;

    if (njs_is_array(this)) {
        array = this->data.u.array;
        nargs = param->nargs;

        if (nargs != 0) {
            if ((intptr_t) nargs > (array->start - array->data)) {
                ret = njs_array_realloc(vm, array, nargs, array->size);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }
            }

            array->length += nargs;
            args = param->args;

            do {
                nargs--;
                /* GC: njs_retain(&args[nargs]); */
                array->start--;
                array->start[0] = args[nargs];
            } while (nargs != 0);
        }

        njs_number_set(&vm->retval, array->length);
    }

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_shift(njs_vm_t *vm, njs_param_t *param)
{
    njs_array_t        *array;
    njs_value_t        *this;
    const njs_value_t  *retval, *value;

    retval = &njs_value_void;

    this = param->this;

    if (njs_is_array(this)) {
        array = this->data.u.array;

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
 */

static njs_ret_t
njs_array_prototype_to_string(njs_vm_t *vm, njs_param_t *param)
{
    njs_value_t         *this;
    njs_object_prop_t   *prop;
    nxt_lvlhsh_query_t  lhq;

    this = param->this;

    if (njs_is_object(this)) {
        lhq.key_hash = NJS_JOIN_HASH;
        lhq.key.len = sizeof("join") - 1;
        lhq.key.data = (u_char *) "join";

        prop = njs_object_property(vm, this->data.u.object, &lhq);

        if (nxt_fast_path(prop != NULL && njs_is_function(&prop->value))) {
            return njs_function_apply(vm, &prop->value, param);
        }
    }

    return njs_object_prototype_to_string(vm, param);
}


static njs_ret_t
njs_array_prototype_join(njs_vm_t *vm, njs_param_t *param)
{
    u_char             *p;
    size_t             size, length;
    nxt_int_t          ret;
    nxt_uint_t         i, n, max;
    njs_array_t        *array;
    njs_value_t        *this, *value, *values;
    njs_string_prop_t  separator, string;

    this = param->this;

    if (!njs_is_array(this)) {
        goto empty;
    }

    array = this->data.u.array;

    if (array->length == 0) {
        goto empty;
    }

    if (param->nargs != 0) {
        value = &param->args[0];

    } else {
        value = (njs_value_t *) &njs_string_comma;
    }

    (void) njs_string_prop(&separator, value);

    max = 0;

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];
        if (njs_is_valid(value) && !njs_is_string(value)) {
            max++;
        }
    }

    values = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                 sizeof(njs_value_t) * max);
    if (nxt_slow_path(values == NULL)) {
        return NXT_ERROR;
    }

    size = separator.size * (array->length - 1);
    length = separator.length * (array->length - 1);
    n = 0;

    for (i = 0; i < array->length; i++) {
        value = &array->start[i];

        if (njs_is_valid(value)) {

            if (!njs_is_string(value)) {
                ret = njs_value_to_string(vm, &values[n], value);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return NXT_ERROR;
                }

                value = &values[n++];
            }

            (void) njs_string_prop(&string, value);

            size += string.size;
            length += string.length;
        }
    }

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

empty:

    vm->retval = njs_string_empty;

    return NXT_OK;
}


static njs_ret_t
njs_array_prototype_concat(njs_vm_t *vm, njs_param_t *param)
{
    size_t       length;
    uintptr_t    nargs;
    nxt_uint_t   i;
    njs_value_t  *this, *args, *value;
    njs_array_t  *array;

    this = param->this;

    if (njs_is_array(this)) {
        length = this->data.u.array->length;

    } else {
        length = 1;
    }

    nargs = param->nargs;
    args = param->args;

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

    value = njs_array_copy(array->start, this);

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
njs_array_prototype_for_each(njs_vm_t *vm, njs_param_t *param)
{
    nxt_int_t         n;
    uintptr_t         nargs;
    njs_param_t       p;
    njs_array_t       *array;
    njs_value_t       *this, *args, *func, arguments[3];
    njs_array_next_t  *next;

    this = param->this;

    if (!vm->frame->reentrant) {
        vm->frame->reentrant = 1;

        if (!njs_is_array(this)) {
            vm->exception = &njs_exception_type_error;
            return NXT_ERROR;
        }

        array = this->data.u.array;
        n = njs_array_next(array->start, 0, array->length);

        if (n < 0) {
            vm->retval = njs_value_void;
            return NXT_OK;
        }

        next = njs_native_data(vm->frame);
        next->index = n;
        next->length = array->length;
    }

    next = njs_native_data(vm->frame);
    n = next->index;

    /* GC: array elt, array */
    array = this->data.u.array;
    arguments[0] = array->start[n];
    njs_number_set(&arguments[1], n);
    arguments[2] = *this;

    n = njs_array_next(array->start, ++n, next->length);
    next->index = n;

    if (n < 0) {
        vm->current += sizeof(njs_vmcode_function_call_t);
    }

    nargs = param->nargs;
    args = param->args;

    p.this = (nargs > 1) ? &args[1] : (njs_value_t *) &njs_value_void;
    p.args = arguments;
    p.nargs = 3;
    p.retval = (njs_index_t) &next->retval;

    func = (nargs != 0) ? &args[0] : (njs_value_t *) &njs_value_void;

    return njs_function_apply(vm, func, &p);
}


static njs_ret_t
njs_array_prototype_some(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t         nargs;
    nxt_int_t         n;
    njs_param_t       p;
    njs_array_t       *array;
    njs_value_t       *this, *args, *func, arguments[3];
    njs_array_next_t  *next;

    this = param->this;

    if (!vm->frame->reentrant) {
        vm->frame->reentrant = 1;

        if (!njs_is_array(this)) {
            vm->exception = &njs_exception_type_error;
            return NXT_ERROR;
        }

        array = this->data.u.array;
        n = njs_array_next(array->start, 0, array->length);
        next = njs_native_data(vm->frame);
        next->index = n;
        next->length = array->length;

    } else {
        next = njs_native_data(vm->frame);

        if (njs_is_true(&next->retval)) {
            vm->retval = njs_value_true;
            return NXT_OK;
        }
    }

    n = next->index;

    if (n < 0) {
        vm->retval = njs_value_false;
        return NXT_OK;
    }

    /* GC: array elt, array */
    array = this->data.u.array;
    arguments[0] = array->start[n];
    njs_number_set(&arguments[1], n);
    arguments[2] = *this;

    next->index = njs_array_next(array->start, ++n, next->length);

    nargs = param->nargs;
    args = param->args;

    p.this = (nargs > 1) ? &args[1] : (njs_value_t *) &njs_value_void;
    p.args = arguments;
    p.nargs = 3;
    p.retval = (njs_index_t) &next->retval;

    func = (nargs != 0) ? &args[0] : (njs_value_t *) &njs_value_void;

    return njs_function_apply(vm, func, &p);
}


static njs_ret_t
njs_array_prototype_every(njs_vm_t *vm, njs_param_t *param)
{
    uintptr_t         nargs;
    nxt_int_t         n;
    njs_param_t       p;
    njs_array_t       *array;
    njs_value_t       *this, *args, *func, arguments[3];
    njs_array_next_t  *next;

    this = param->this;

    if (!vm->frame->reentrant) {
        vm->frame->reentrant = 1;

        if (!njs_is_array(this)) {
            vm->exception = &njs_exception_type_error;
            return NXT_ERROR;
        }

        array = this->data.u.array;
        n = njs_array_next(array->start, 0, array->length);
        next = njs_native_data(vm->frame);
        next->index = n;
        next->length = array->length;

    } else {
        next = njs_native_data(vm->frame);

        if (!njs_is_true(&next->retval)) {
            vm->retval = njs_value_false;
            return NXT_OK;
        }
    }

    n = next->index;

    if (n < 0) {
        vm->retval = njs_value_true;
        return NXT_OK;
    }

    /* GC: array elt, array */
    array = this->data.u.array;
    arguments[0] = array->start[n];
    njs_number_set(&arguments[1], n);
    arguments[2] = *this;

    next->index = njs_array_next(array->start, ++n, next->length);

    nargs = param->nargs;
    args = param->args;

    p.this = (nargs > 1) ? &args[1] : (njs_value_t *) &njs_value_void;
    p.args = arguments;
    p.nargs = 3;
    p.retval = (njs_index_t) &next->retval;

    func = (nargs != 0) ? &args[0] : (njs_value_t *) &njs_value_void;

    return njs_function_apply(vm, func, &p);
}


static nxt_noinline nxt_int_t
njs_array_next(njs_value_t *value, nxt_uint_t n, nxt_uint_t length)
{
    while (n < length) {
        if (njs_is_valid(&value[n])) {
            return n;
        }

        n++;
    }

    return -1;
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
        .value = njs_native_function(njs_array_prototype_to_string, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("join"),
        .value = njs_native_function(njs_array_prototype_join, 0,
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
                     njs_method_data_size(sizeof(njs_array_next_t)), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("some"),
        .value = njs_native_function(njs_array_prototype_some,
                     njs_method_data_size(sizeof(njs_array_next_t)), 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("every"),
        .value = njs_native_function(njs_array_prototype_every,
                     njs_method_data_size(sizeof(njs_array_next_t)), 0),
    },
};


const njs_object_init_t  njs_array_prototype_init = {
    njs_array_prototype_properties,
    nxt_nitems(njs_array_prototype_properties),
};
