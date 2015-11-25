
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_stub.h>
#include <nxt_utf8.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_malloc.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_object_hash.h>
#include <njs_array.h>
#include <njs_function.h>
#include <njs_regexp.h>
#include <njs_regexp_pattern.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <string.h>


static nxt_noinline void njs_string_slice_prop(njs_param_t *param,
    njs_string_prop_t *string, njs_slice_prop_t *slice);
static nxt_noinline void njs_string_slice_params(njs_param_t *param,
    njs_slice_prop_t *slice);
static nxt_noinline ssize_t njs_string_index_of(njs_vm_t *vm,
    njs_value_t *src, njs_value_t *search_string, size_t index);


njs_ret_t
njs_string_create(njs_vm_t *vm, njs_value_t *value, u_char *start, size_t size,
    size_t length)
{
    u_char        *dst, *src;
    njs_string_t  *string;

    value->type = NJS_STRING;
    njs_string_truth(value, size);

    if (size <= NJS_STRING_SHORT) {
        value->short_string.size = size;
        value->short_string.length = length;

        dst = value->short_string.start;
        src = start;

        while (size != 0) {
            /* The maximum size is just 14 bytes. */
            nxt_pragma_loop_disable_vectorization;

            *dst++ = *src++;
            size--;
        }

    } else {
        /*
         * Setting UTF-8 length is not required here, it just allows
         * to store the constant in whole byte instead of bit twiddling.
         */
        value->short_string.size = NJS_STRING_LONG;
        value->short_string.length = 0;
        value->data.external0 = 0xff;
        value->data.string_size = size;

        string = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_string_t));
        if (nxt_slow_path(string == NULL)) {
            return NXT_ERROR;
        }

        value->data.u.string = string;

        string->start = start;
        string->length = length;
        string->retain = 1;
    }

    return NXT_OK;
}


nxt_noinline u_char *
njs_string_alloc(njs_vm_t *vm, njs_value_t *value, uint32_t size,
    uint32_t length)
{
    uint32_t      total;
    njs_string_t  *string;

    value->type = NJS_STRING;
    njs_string_truth(value, size);

    if (size <= NJS_STRING_SHORT) {
        value->short_string.size = size;
        value->short_string.length = length;

        return value->short_string.start;
    }

    /*
     * Setting UTF-8 length is not required here, it just allows
     * to store the constant in whole byte instead of bit twiddling.
     */
    value->short_string.size = NJS_STRING_LONG;
    value->short_string.length = 0;
    value->data.external0 = 0;
    value->data.string_size = size;

    if (size != length && length > NJS_STRING_MAP_OFFSET) {
        total = nxt_align_size(size, sizeof(uint32_t));
        total += ((length - 1) / NJS_STRING_MAP_OFFSET) * sizeof(uint32_t);

    } else {
        total = size;
    }

    string = nxt_mem_cache_alloc(vm->mem_cache_pool,
                                 sizeof(njs_string_t) + total);

    if (nxt_fast_path(string != NULL)) {
        value->data.u.string = string;

        string->start = (u_char *) string + sizeof(njs_string_t);
        string->length = length;
        string->retain = 1;

        return string->start;
    }

    return NULL;
}


void
njs_string_copy(njs_value_t *dst, njs_value_t *src)
{
    *dst = *src;

    /* GC: long string retain */
}


/*
 * njs_string_validate() validates an UTF-8 string, evaluates its length,
 * sets njs_string_prop_t struct, and initializes offset map if it is required.
 */

nxt_noinline njs_ret_t
njs_string_validate(njs_vm_t *vm, njs_string_prop_t *string, njs_value_t *value)
{
    u_char   *start;
    size_t   new_size;
    ssize_t  size, length;

    size = value->short_string.size;

    if (size != NJS_STRING_LONG) {
        string->start = value->short_string.start;
        length = value->short_string.length;

        if (length == 0 && length != size) {
            length = nxt_utf8_length(value->short_string.start, size);

            if (nxt_slow_path(length < 0)) {
                /* Invalid UTF-8 string. */
                return length;
            }

            value->short_string.length = length;
        }

    } else {
        string->start = value->data.u.string->start;
        size = value->data.string_size;
        length = value->data.u.string->length;

        if (length == 0 && length != size) {
            length = nxt_utf8_length(string->start, size);

            if (length != size) {
                if (nxt_slow_path(length < 0)) {
                    /* Invalid UTF-8 string. */
                    return length;
                }

                if (length > NJS_STRING_MAP_OFFSET) {
                    /*
                     * Reallocate the long string with offset map
                     * after the string.
                     */
                    new_size = nxt_align_size(size, sizeof(uint32_t));
                    new_size += ((length - 1) / NJS_STRING_MAP_OFFSET)
                                * sizeof(uint32_t);

                    start = nxt_mem_cache_alloc(vm->mem_cache_pool, new_size);
                    if (nxt_slow_path(start == NULL)) {
                        return NXT_ERROR;
                    }

                    memcpy(start, string->start, size);
                    string->start = start;
                    value->data.u.string->start = start;

                    njs_string_offset_map_init(start, size);
                }
            }

            value->data.u.string->length = length;
        }
    }

    string->size = size;
    string->length = length;

    return length;
}


nxt_noinline size_t
njs_string_prop(njs_string_prop_t *string, njs_value_t *value)
{
    size_t     size;
    uintptr_t  length;

    size = value->short_string.size;

    if (size != NJS_STRING_LONG) {
        string->start = value->short_string.start;
        length = value->short_string.length;

    } else {
        string->start = value->data.u.string->start;
        size = value->data.string_size;
        length = value->data.u.string->length;
    }

    string->size = size;
    string->length = length;

    return (length == 0) ? size : length;
}


njs_ret_t
njs_string_ctor_function(njs_vm_t *vm, njs_param_t *param)
{
    njs_object_t       *object;
    const njs_value_t  *value;

    if (param->nargs == 0) {
        value = &njs_string_empty;

    } else {
        /* TODO: to_string. */
        value = &param->args[0];
    }

    if (vm->frame->ctor) {
        /* value->type is the same as prototype offset. */
        object = njs_object_value_alloc(vm, value, value->type);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

        vm->retval.data.u.object = object;
        vm->retval.type = NJS_OBJECT_STRING;
        vm->retval.data.truth = 1;

    } else {
        vm->retval = *value;
    }

    return NXT_OK;
}


static const njs_object_prop_t  njs_string_function_properties[] =
{
    /* String.name == "String". */
    { njs_string("String"),
      njs_string("name"),
      NJS_PROPERTY, 0, 0, 0, },

    /* String.length == 1. */
    { njs_value(NJS_NUMBER, 1, 1.0),
      njs_string("length"),
      NJS_PROPERTY, 0, 0, 0, },

    /* String.prototype. */
    { njs_getter(njs_object_prototype_create_prototype),
      njs_string("prototype"),
      NJS_NATIVE_GETTER, 0, 0, 0, },
};


nxt_int_t
njs_string_function_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash)
{
    return njs_object_hash_create(vm, hash, njs_string_function_properties,
                                  nxt_nitems(njs_string_function_properties));
}


static njs_ret_t
njs_string_prototype_length(njs_vm_t *vm, njs_value_t *value)
{
    size_t     size;
    uintptr_t  length;

    length = 0;

    /* TODO: String object. */

    if (njs_is_string(value)) {
        size = value->short_string.size;
        length = value->short_string.length;

        if (size == NJS_STRING_LONG) {
            size = value->data.string_size;
            length = value->data.u.string->length;
        }

        length = (length == 0) ? size : length;
    }

    njs_number_set(&vm->retval, length);

    njs_release(vm, value);

    return NXT_OK;
}


nxt_noinline void
njs_string_offset_map_init(const u_char *start, size_t size)
{
    size_t        offset;
    uint32_t      *map;
    nxt_uint_t    n;
    const u_char  *p, *end;

    end = start + size;
    map = (uint32_t *) nxt_align_ptr(end, sizeof(uint32_t));
    p = start;
    n = 0;
    offset = NJS_STRING_MAP_OFFSET;

    do {
        if (offset == 0) {
            map[n++] = p - start;
            offset = NJS_STRING_MAP_OFFSET;
        }

        /* The UTF-8 string should be valid since its length is known. */
        p = nxt_utf8_next(p, end);

        offset--;

    } while (p < end);
}


nxt_bool_t
njs_string_eq(const njs_value_t *v1, const njs_value_t *v2)
{
    size_t        size;
    const u_char  *start1, *start2;

    size = v1->short_string.size;

    if (size != v2->short_string.size) {
        return 0;
    }

    if (size != NJS_STRING_LONG) {
        start1 = v1->short_string.start;
        start2 = v2->short_string.start;

    } else {
        size = v1->data.string_size;

        if (size != v2->data.string_size) {
            return 0;
        }

        start1 = v1->data.u.string->start;
        start2 = v2->data.u.string->start;
    }

    return (memcmp(start1, start2, size) == 0);
}


nxt_int_t
njs_string_cmp(const njs_value_t *v1, const njs_value_t *v2)
{
    size_t        size, size1, size2;
    nxt_int_t     ret;
    const u_char  *start1, *start2;

    size1 = v1->short_string.size;

    if (size1 != NJS_STRING_LONG) {
        start1 = v1->short_string.start;

    } else {
        size1 = v1->data.string_size;
        start1 = v1->data.u.string->start;
    }

    size2 = v2->short_string.size;

    if (size2 != NJS_STRING_LONG) {
        start2 = v2->short_string.start;

    } else {
        size2 = v2->data.string_size;
        start2 = v2->data.u.string->start;
    }

    size = nxt_min(size1, size2);

    ret = memcmp(start1, start2, size);

    if (ret != 0) {
        return ret;
    }

    return (size1 - size2);
}


static njs_ret_t
njs_string_prototype_value_of(njs_vm_t *vm, njs_param_t *param)
{
    njs_value_t         *value;
    njs_object_value_t  *object;

    value = param->object;

    if (value->type == NJS_STRING) {
        vm->retval = *value;

    } else if (value->type == NJS_OBJECT_STRING) {
        object = value->data.u.object_value;
        vm->retval = object->value;

    } else {
        vm->exception = &njs_exception_type_error;
        return NXT_ERROR;
    }

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_concat(njs_vm_t *vm, njs_param_t *param)
{
    u_char             *p, *start;
    size_t             size, length, mask;
    uintptr_t          nargs;
    njs_ret_t          ret;
    nxt_uint_t         i;
    njs_value_t        *object, *args, *values;
    njs_string_prop_t  string;

    object = param->object;
    nargs = param->nargs;

    if (nargs == 0) {
        njs_string_copy(&vm->retval, object);
        return NXT_OK;
    }

    values = alloca((nargs + 1) * sizeof(njs_value_t));

    ret = njs_value_to_string(vm, &values[0], object);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    (void) njs_string_prop(&string, &values[0]);

    size = string.size;
    length = string.length;

    mask = (string.length == 0 && string.size != 0) ? 0 : -1;

    args = param->args;

    for (i = 0; i < nargs; i++) {
        ret = njs_value_to_string(vm, &values[i + 1], &args[i]);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }

        (void) njs_string_prop(&string, &values[i + 1]);

        size += string.size;
        length += string.length;

        if (string.length == 0 && string.size != 0) {
            mask = 0;
        }
    }

    length &= mask;

    start = njs_string_alloc(vm, &vm->retval, size, length);

    if (nxt_slow_path(start == NULL)) {
        return NXT_ERROR;
    }

    p = start;

    for (i = 0; i <= nargs; i++) {
        (void) njs_string_prop(&string, &values[i]);

        p = memcpy(p, string.start, string.size);
        p += string.size;
    }

    if (length >= NJS_STRING_MAP_OFFSET && size != length) {
        njs_string_offset_map_init(start, size);
    }

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_from_utf8(njs_vm_t *vm, njs_param_t *param)
{
    u_char             *p;
    ssize_t            length;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_string_slice_prop(param, &string, &slice);

    if (string.length != 0) {
        /* ASCII or UTF8 string. */
        return njs_string_slice(vm, &vm->retval, &string, &slice);
    }

    string.start += slice.start;

    length = nxt_utf8_length(string.start, slice.length);

    if (length >= 0) {

        if (length < NJS_STRING_MAP_OFFSET || (size_t) length == slice.length) {
            /* ASCII or short UTF-8 string. */
            return njs_string_create(vm, &vm->retval, string.start,
                                     slice.length, length);
        }

        /* Long UTF-8 string. */

        p = njs_string_alloc(vm, &vm->retval, slice.length, length);

        if (nxt_fast_path(p != NULL)) {
            memcpy(p, string.start, slice.length);
            njs_string_offset_map_init(p, slice.length);

            return NXT_OK;
        }

        return NXT_ERROR;
    }

    vm->retval = njs_value_null;

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_to_utf8(njs_vm_t *vm, njs_param_t *param)
{
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    (void) njs_string_prop(&string, param->object);

    string.length = 0;
    slice.string_length = string.size;

    njs_string_slice_params(param, &slice);

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


static njs_ret_t
njs_string_prototype_from_bytes(njs_vm_t *vm, njs_param_t *param)
{
    u_char             *p, *s, *start, *end;
    size_t             size;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_string_slice_prop(param, &string, &slice);

    if (string.length != 0) {
        /* ASCII or UTF8 string. */
        return njs_string_slice(vm, &vm->retval, &string, &slice);
    }

    size = 0;
    string.start += slice.start;
    end = string.start + slice.length;

    for (p = string.start; p < end; p++) {
        size += (*p < 0x80) ? 1 : 2;
    }

    start = njs_string_alloc(vm, &vm->retval, size, slice.length);

    if (nxt_fast_path(start != NULL)) {

        if (size == slice.length) {
            memcpy(start, string.start, size);

        } else {
            s = start;
            end = string.start + slice.length;

            for (p = string.start; p < end; p++) {
                s = nxt_utf8_encode(s, *p);
            }

            if (slice.length >= NJS_STRING_MAP_OFFSET || size != slice.length) {
                njs_string_offset_map_init(start, size);
            }
        }

        return NXT_OK;
    }

    return NXT_ERROR;
}


static njs_ret_t
njs_string_prototype_to_bytes(njs_vm_t *vm, njs_param_t *param)
{
    u_char             *p;
    size_t             length;
    uint32_t           byte;
    const u_char       *s, *end;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_string_slice_prop(param, &string, &slice);

    if (string.length == 0) {
        /* Byte string. */
        return njs_string_slice(vm, &vm->retval, &string, &slice);
    }

    p = njs_string_alloc(vm, &vm->retval, slice.length, 0);

    if (nxt_fast_path(p != NULL)) {

        if (string.length != 0) {
            /* UTF-8 string. */
            end = string.start + string.size;

            s = njs_string_offset(string.start, end, slice.start);

            length = slice.length;

            while (length != 0 && s < end) {
                byte = nxt_utf8_decode(&s, end);

                if (nxt_slow_path(byte > 0xFF)) {
                    njs_release(vm, &vm->retval);
                    vm->retval = njs_value_null;

                    return NXT_OK;
                }

                *p++ = (u_char) byte;
                length--;
            }

        } else {
            /* ASCII string. */
            memcpy(p, string.start + slice.start, slice.length);
        }

        return NXT_OK;
    }

    return NXT_ERROR;
}


/*
 * String.slice(start[, end]).
 * JavaScript 1.2, ECMAScript 3.
 */

static nxt_noinline njs_ret_t
njs_string_prototype_slice(njs_vm_t *vm, njs_param_t *param)
{
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_string_slice_prop(param, &string, &slice);

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


/*
 * String.substring(start[, end]).
 * JavaScript 1.0, ECMAScript 1.
 */

static njs_ret_t
njs_string_prototype_substring(njs_vm_t *vm, njs_param_t *param)
{
    ssize_t            start, end, length;
    uintptr_t          nargs;
    njs_value_t        *args;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    length = njs_string_prop(&string, param->object);

    slice.string_length = length;
    start = 0;
    nargs = param->nargs;

    if (nargs != 0) {
        args = param->args;

        start = njs_value_to_number(&args[0]);

        if (start < 0) {
            start = 0;
        }

        if (nargs > 1) {
            end = njs_value_to_number(&args[1]);

            if (end < 0) {
                end = 0;
            }

            length = end - start;

            if (length < 0) {
                length = -length;
                start = end;
            }
        }
    }

    slice.start = start;
    slice.length = length;

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


/*
 * String.substr(start[, length]).
 * JavaScript 1.0, ECMAScript 3.
 */

static njs_ret_t
njs_string_prototype_substr(njs_vm_t *vm, njs_param_t *param)
{
    ssize_t            start, length;
    uintptr_t          nargs;
    njs_value_t        *args;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    length = njs_string_prop(&string, param->object);

    slice.string_length = length;
    start = 0;
    nargs = param->nargs;

    if (nargs != 0) {
        args = param->args;

        start = njs_value_to_number(&args[0]);

        if (start < 0) {

            start += length;
            if (start < 0) {
                start = 0;
            }
        }

        if (nargs > 1) {
            length = njs_value_to_number(&args[1]);
        }
    }

    slice.start = start;
    slice.length = length;

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


static njs_ret_t
njs_string_prototype_char_at(njs_vm_t *vm, njs_param_t *param)
{
    ssize_t            start, length;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    slice.string_length = njs_string_prop(&string, param->object);

    start = 0;
    length = 1;

    if (param->nargs != 0) {
        start = njs_value_to_number(&param->args[0]);

        if (start < 0) {
            length = 0;
        }
    }

    slice.start = start;
    slice.length = length;

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


static nxt_noinline void
njs_string_slice_prop(njs_param_t *param, njs_string_prop_t *string,
    njs_slice_prop_t *slice)
{
    slice->string_length = njs_string_prop(string, param->object);

    njs_string_slice_params(param, slice);
}


static nxt_noinline void
njs_string_slice_params(njs_param_t *param, njs_slice_prop_t *slice)
{
    ssize_t      start, end, length;
    uintptr_t    nargs;
    njs_value_t  *args;

    length = slice->string_length;
    start = 0;
    nargs = param->nargs;

    if (nargs != 0) {
        args = param->args;

        start = njs_value_to_number(&args[0]);

        if (start < 0) {
            start += length;

            if (start < 0) {
                start = 0;
            }
        }

        end = length;

        if (nargs > 1) {
            end = njs_value_to_number(&args[1]);

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

    slice->start = start;
    slice->length = length;
}


nxt_noinline njs_ret_t
njs_string_slice(njs_vm_t *vm, njs_value_t *dst,
    const njs_string_prop_t *string, njs_slice_prop_t *slice)
{
    u_char        *s;
    size_t        size, n, length;
    ssize_t       excess;
    const u_char  *p, *start, *end;

    length = slice->length;

    if (length > 0 && slice->start < slice->string_length) {

        start = string->start;
        end = start + string->size;

        if (string->size == slice->string_length) {
            /* Byte or ASCII string. */
            start += slice->start;

            excess = (start + length) - end;
            if (excess > 0) {
                length -= excess;
            }

            size = length;

            if (string->length == 0) {
                length = 0;
            }

        } else {
            /* UTF-8 string. */
            start = njs_string_offset(start, end, slice->start);

            /* Evaluate size of the slice in bytes and ajdust length. */
            p = start;
            n = length;

            do {
                p = nxt_utf8_next(p, end);
                n--;
            } while (n != 0 && p < end);

            size = p - start;
            length -= n;
        }

        if (nxt_fast_path(size != 0)) {
            s = njs_string_alloc(vm, &vm->retval, size, length);

            if (nxt_slow_path(s == NULL)) {
                return NXT_ERROR;
            }

            memcpy(s, start, size);

            if (length >= NJS_STRING_MAP_OFFSET && size != length) {
                njs_string_offset_map_init(s, size);
            }

            return NXT_OK;
        }
    }

    vm->retval = njs_string_empty;

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_char_code_at(njs_vm_t *vm, njs_param_t *param)
{
    double             num;
    ssize_t            index, length;
    uint32_t           code;
    const u_char       *start, *end;
    njs_string_prop_t  string;

    length = njs_string_prop(&string, param->object);

    index = 0;

    if (param->nargs != 0) {
        index = njs_value_to_number(&param->args[0]);

        if (nxt_slow_path(index < 0 || index >= length)) {
            num = NJS_NAN;
            goto done;
        }
    }

    if ((uint32_t) length == string.size) {
        /* Byte or ASCII string. */
        code = string.start[index];

    } else {
        /* UTF-8 string. */
        end = string.start + string.size;
        start = njs_string_offset(string.start, end, index);
        code = nxt_utf8_decode(&start, end);
    }

    num = code;

done:

    njs_number_set(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_index_of(njs_vm_t *vm, njs_param_t *param)
{
    ssize_t      start, index;
    uintptr_t    nargs;
    njs_value_t  *args;

    index = -1;
    nargs = param->nargs;

    if (nargs != 0) {
        start = 0;
        args = param->args;

        if (nargs > 1) {
            start = njs_value_to_number(&args[1]);

            if (start < 0) {
                start = 0;
            }
        }

        index = njs_string_index_of(vm, param->object, &args[0], start);
    }

    njs_number_set(&vm->retval, index);

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_last_index_of(njs_vm_t *vm, njs_param_t *param)
{
    ssize_t      ret, index, last;
    uintptr_t    nargs;
    njs_value_t  *args;

    index = -1;
    nargs = param->nargs;

    if (nargs != 0) {
        last = NJS_STRING_MAX_LENGTH;
        args = param->args;

        if (nargs > 1) {
            last = njs_value_to_number(&args[1]);

            if (last < 0) {
                last = 0;
            }
        }

        ret = 0;

        for ( ;; ) {
            ret = njs_string_index_of(vm, param->object, &args[0], ret);

            if (ret < 0 || ret >= last) {
                break;
            }

            index = ret++;
        }
    }

    njs_number_set(&vm->retval, index);

    return NXT_OK;
}


static nxt_noinline ssize_t
njs_string_index_of(njs_vm_t *vm, njs_value_t *src, njs_value_t *search_string,
    size_t index)
{
    size_t             length;
    const u_char       *p, *end;
    njs_string_prop_t  string, search;

    (void) njs_string_prop(&search, search_string);

    length = njs_string_prop(&string, src);

    if (index < length) {

        p = string.start;
        end = p + string.size;

        if (string.size == length) {
            /* Byte or ASCII string. */
            p += index;

        } else {
            /* UTF-8 string. */
            p = njs_string_offset(p, end, index);
        }

        while (p < end) {
            if (memcmp(p, search.start, search.size) == 0) {
                return index;
            }

            index++;
            p = nxt_utf8_next(p, end);
        }

    } else if (search.size == 0) {
        return length;
    }

    return -1;
}


/*
 * njs_string_offset() assumes that index is correct
 * and the optional offset map has been initialized.
 */

nxt_noinline const u_char *
njs_string_offset(const u_char *start, const u_char *end, size_t index)
{
    uint32_t    *map;
    nxt_uint_t  skip;

    if (index >= NJS_STRING_MAP_OFFSET) {
        map = (uint32_t *) nxt_align_ptr(end, sizeof(uint32_t));

        start += map[index / NJS_STRING_MAP_OFFSET - 1];
    }

    for (skip = index % NJS_STRING_MAP_OFFSET; skip != 0; skip--) {
        start = nxt_utf8_next(start, end);
    }

    return start;
}


/*
 * njs_string_index() assumes that offset is correct
 * and the optional offset map has been initialized.
 */

nxt_noinline uint32_t
njs_string_index(njs_string_prop_t *string, uint32_t offset)
{
    uint32_t      *map, last, index;
    const u_char  *p, *start, *end;

    if (string->size == string->length) {
        return offset;
    }

    last = 0;
    index = 0;

    if (string->length >= NJS_STRING_MAP_OFFSET) {

        end = string->start + string->size;
        map = (uint32_t *) nxt_align_ptr(end, sizeof(uint32_t));

        while (index + NJS_STRING_MAP_OFFSET < string->length
               && *map <= offset)
        {
            last = *map++;
            index += NJS_STRING_MAP_OFFSET;
        }
    }

    p = string->start + last;
    start = string->start + offset;
    end = string->start + string->size;

    while (p < start) {
        index++;
        p = nxt_utf8_next(p, end);
    }

    return index;
}


static njs_ret_t
njs_string_prototype_search(njs_vm_t *vm, njs_param_t *param)
{
    int                   ret;
    nxt_int_t             index;
    nxt_uint_t            n;
    njs_value_t           *args;
    njs_string_prop_t     string;
    njs_regexp_pattern_t  *pattern;
    int                   captures[3];

    /* TODO: convert object to String. */

    index = 0;

    if (param->nargs != 0) {

        args = param->args;

        switch (args[0].type) {

        case NJS_REGEXP:
            pattern = args[0].data.u.regexp->pattern;
            break;

        case NJS_STRING:
            (void) njs_string_prop(&string, &args[0]);

            pattern = njs_regexp_pattern_create(vm, string.start,
                                                string.length, 0);
            if (nxt_slow_path(pattern == NULL)) {
                return NXT_ERROR;
            }

            break;

        case NJS_VOID:
            /* STUB: precompiled "/(?:)/" pattern. */
            string.start = (u_char *) "(?:)";
            string.length = sizeof("(?:)") - 1;

            pattern = njs_regexp_pattern_create(vm, string.start,
                                                string.length, 0);
            if (nxt_slow_path(pattern == NULL)) {
                return NXT_ERROR;
            }

            break;

        default:
            /* STUB: convert args[0] to String, then to RegExp. */
            vm->exception = &njs_exception_type_error;

            return NXT_ERROR;
        }

        index = -1;

        (void) njs_string_prop(&string, param->object);

        n = (string.length != 0 && string.length != string.size);

        if (pattern->code[n] != NULL) {
            ret = pcre_exec(pattern->code[n], pattern->extra[n],
                            (char *) string.start, string.size,
                            0, 0, captures, 3);

            if (ret >= 0) {
                index = njs_string_index(&string, captures[0]);

            } else if (ret != PCRE_ERROR_NOMATCH) {
                /* TODO: exception */
                return NXT_ERROR;
            }
        }
    }

    njs_number_set(&vm->retval, index);

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_match(njs_vm_t *vm, njs_param_t *param)
{
    u_char                *start;
    int32_t               size, length;
    njs_ret_t             ret;
    nxt_uint_t            n, utf8;
    njs_value_t           *args;
    njs_array_t           *array;
    njs_regexp_t          *regexp;
    njs_string_prop_t     string;
    njs_regexp_pattern_t  *pattern;
    int                   captures[3];

    /* TODO: empty regexp */

    args = param->args;
    regexp = args[0].data.u.regexp;

    if (!regexp->pattern->global) {
        /*
         * string.match(regexp) is the same as regexp.exec(string)
         * if the regexp has no global flag.
         */
        param->args = param->object;
        param->object = args;
        param->nargs = 1;

        return njs_regexp_prototype_exec(vm, param);
    }

    vm->retval = njs_value_null;

    (void) njs_string_prop(&string, param->object);

    utf8 = 0;
    n = 0;

    if (string.length != 0) {
        utf8 = 1;
        n = 1;

        if (string.length != string.size) {
            utf8 = 2;
        }
    }

    pattern = regexp->pattern;

    if (pattern->code[n] != NULL) {
        array = NULL;

        if (n != 0) {
            utf8 = 2;

        } else if (string.length != 0) {
            utf8 = 1;

        } else {
            utf8 = 1;
        }

        do {
            ret = pcre_exec(pattern->code[n], pattern->extra[n],
                            (char *) string.start, string.size,
                            0, 0, captures, 3);

            if (ret >= 0) {
                if (array != NULL) {
                    if (array->length == array->size) {
                        ret = njs_array_realloc(vm, array, 0, array->size + 1);
                        if (nxt_slow_path(ret != NXT_OK)) {
                            return ret;
                        }
                    }

                } else {
                    array = njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
                    if (nxt_slow_path(array == NULL)) {
                        return NXT_ERROR;
                    }

                    vm->retval.data.u.array = array;
                    vm->retval.type = NJS_ARRAY;
                    vm->retval.data.truth = 1;
                }

                start = &string.start[captures[0]];

                string.start += captures[1];
                string.size -= captures[1];

                size = captures[1] - captures[0];

                switch (utf8) {
                case 0:
                    length = 0;
                    break;
                case 1:
                    length = size;
                    break;
                default:
                    length = nxt_utf8_length(start, size);
                    break;
                }

                ret = njs_string_create(vm, &array->start[array->length],
                                        start, size, length);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                array->length++;

            } else if (ret == PCRE_ERROR_NOMATCH) {
                break;

            } else {
                /* TODO: internal error exception */
                return NXT_ERROR;
            }

        } while (string.size > 0);
    }

    regexp->last_index = 0;

    return NXT_OK;
}


njs_ret_t
njs_value_to_string(njs_vm_t *vm, njs_value_t *dst, const njs_value_t *src)
{
    njs_ret_t           ret;
    njs_param_t         param;
    njs_object_prop_t   *prop;
    const njs_value_t   *value;
    nxt_lvlhsh_query_t  lhq;

    switch (src->type) {

    case NJS_NULL:
        value = &njs_string_null;
        break;

    case NJS_VOID:
        value = &njs_string_void;
        break;

    case NJS_BOOLEAN:
        value = njs_is_true(src) ? &njs_string_true : &njs_string_false;
        break;

    case NJS_NUMBER:
        return njs_number_to_string(vm, dst, src);

    case NJS_STRING:
        /* GC: njs_retain(src); */
        value = src;
        break;

    case NJS_OBJECT:
    case NJS_ARRAY:
    case NJS_FUNCTION:
    case NJS_REGEXP:
        lhq.key_hash = NJS_TO_STRING_HASH;
        lhq.key.len = sizeof("toString") - 1;
        lhq.key.data = (u_char *) "toString";

        prop = njs_object_property(vm, &src->data.u.array->object, &lhq);

        if (nxt_fast_path(prop != NULL)) {
            param.object = (njs_value_t *) src;
            param.args = NULL;
            param.nargs = 0;
            param.retval = (njs_index_t) dst;

            ret = njs_function_apply(vm, &prop->value, &param);
            if (nxt_fast_path(ret == 0)) {
                *dst = vm->retval;
                return ret;
            }
        }

        return NXT_ERROR;

        /*
         * TODO:
         *   function, regexp: find "toString()" in prototype chain:
         *      function: full function text.
         *      regex:    full regexp text like "/regexp/gim".
         */

    case NJS_NATIVE:
    case NJS_EXTERNAL:
        value = &njs_string_native;
        break;

    default:  /* NJS_INVALID */
        return NXT_ERROR;
    }

    *dst = *value;

    return NXT_OK;
}


njs_ret_t
njs_value_to_ext_string(njs_vm_t *vm, nxt_str_t *dst, const njs_value_t *src)
{
    u_char       *start;
    size_t       size;
    njs_ret_t    ret;
    njs_value_t  value;

    if (nxt_fast_path(src != NULL)) {
        ret = njs_value_to_string(vm, &value, src);

        if (nxt_fast_path(ret == NXT_OK)) {
            size = value.short_string.size;

            if (size != NJS_STRING_LONG) {
                start = nxt_mem_cache_alloc(vm->mem_cache_pool, size);
                if (nxt_slow_path(start == NULL)) {
                    return NXT_ERROR;
                }

                memcpy(start, value.short_string.start, size);

            } else {
                size = value.data.string_size;
                start = value.data.u.string->start;
            }

            dst->len = size;
            dst->data = start;
        }

        return ret;
    }

    dst->len = 0;
    dst->data = NULL;

    return NXT_OK;
}


double
njs_string_to_number(njs_value_t *value)
{
    double        num;
    size_t        size;
    nxt_bool_t    minus;
    const u_char  *p, *end;

    size = value->short_string.size;

    if (size != NJS_STRING_LONG) {
        p = value->short_string.start;

    } else {
        size = value->data.string_size;
        p = value->data.u.string->start;
    }

    end = p + size;

    while (p < end) {
        if (*p != ' ' && *p != '\t') {
            break;
        }

        p++;
    }

    if (p == end) {
        return 0.0;
    }

    minus = 0;

    if (*p == '+') {
        p++;

    } else if (*p == '-') {
        p++;
        minus = 1;
    }

    if (*p >= '0' && *p <= '9') {
        num = njs_number_parse(&p, end);

    } else {
        return NJS_NAN;
    }

    while (p < end) {
        if (*p != ' ' && *p != '\t') {
            return NJS_NAN;
        }

        p++;
    }

    return minus ? -num : num;
}


static const njs_object_prop_t  njs_string_prototype_properties[] =
{
    { njs_getter(njs_primitive_prototype_get_proto),
      njs_string("__proto__"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_getter(njs_string_prototype_length),
      njs_string("length"),
      NJS_NATIVE_GETTER, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_value_of, 0),
      njs_string("valueOf"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_value_of, 0),
      njs_string("toString"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_concat, 0),
      njs_string("concat"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_from_utf8, 0),
      njs_string("fromUTF8"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_to_utf8, 0),
      njs_string("toUTF8"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_from_bytes, 0),
      njs_string("fromBytes"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_to_bytes, 0),
      njs_string("toBytes"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_slice, 0),
      njs_string("slice"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_substring, 0),
      njs_string("substring"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_substr, 0),
      njs_string("substr"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_char_at, 0),
      njs_string("charAt"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_char_code_at, 0),
      njs_string("charCodeAt"),
      NJS_METHOD, 0, 0, 0, },

    /* ECMAScript 6, codePointAt(). */

    { njs_native_function(njs_string_prototype_char_code_at, 0),
      njs_string("codePointAt"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_index_of, 0),
      njs_string("indexOf"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_last_index_of, 0),
      njs_string("lastIndexOf"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_search, 0),
      njs_string("search"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_string_prototype_match, 0),
      njs_string("match"),
      NJS_METHOD, 0, 0, 0, },
};


nxt_int_t
njs_string_prototype_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash)
{
    return njs_object_hash_create(vm, hash, njs_string_prototype_properties,
                                  nxt_nitems(njs_string_prototype_properties));
}


static nxt_int_t
njs_values_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_value_t  *value;

    value = data;

    if (lhq->key.len == sizeof(njs_value_t)
        && memcmp(lhq->key.data, value, sizeof(njs_value_t)) == 0)
    {
        return NXT_OK;
    }

    if (value->type == NJS_STRING
        && value->data.string_size == lhq->key.len
        && memcmp(value->data.u.string->start, lhq->key.data, lhq->key.len)
           == 0)
    {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


static const nxt_lvlhsh_proto_t  njs_values_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_values_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


/*
 * Constant values such as njs_value_zero are copied to vm->values_hash during
 * code generation when them are used as operands to guarantee aligned value.
 */

njs_index_t
njs_value_index(njs_vm_t *vm, njs_parser_t *parser, const njs_value_t *src)
{
    u_char              *start;
    uint32_t            value_size, size, length;
    nxt_int_t           ret;
    njs_value_t         *value;
    njs_string_t        *string;
    nxt_lvlhsh_query_t  lhq;

    if (src->type != NJS_STRING || src->short_string.size != NJS_STRING_LONG) {
        size = sizeof(njs_value_t);
        start = (u_char *) src;

    } else {
        size = src->data.string_size;
        start = src->data.u.string->start;
    }

    lhq.key_hash = nxt_djb_hash(start, size);
    lhq.key.len = size;
    lhq.key.data = start;
    lhq.proto = &njs_values_hash_proto;

    if (nxt_lvlhsh_find(&vm->values_hash, &lhq) == NXT_OK) {
        value = lhq.value;

    } else if (nxt_lvlhsh_find(&parser->values_hash, &lhq) == NXT_OK) {
        value = lhq.value;

    } else {
        value_size = 0;

        if (start != (u_char *) src) {
            /* Long string value is allocated together with string. */
            value_size = sizeof(njs_value_t) + sizeof(njs_string_t);

            length = src->data.u.string->length;

            if (size != length && length > NJS_STRING_MAP_OFFSET) {
                size = nxt_align_size(size, sizeof(uint32_t));
                size += ((length - 1) / NJS_STRING_MAP_OFFSET)
                        * sizeof(uint32_t);
            }
        }

        value = nxt_mem_cache_align(vm->mem_cache_pool, sizeof(njs_value_t),
                                    value_size + size);
        if (nxt_slow_path(value == NULL)) {
            return NJS_INDEX_NONE;
        }

        *value = *src;

        if (start != (u_char *) src) {
            string = (njs_string_t *) ((u_char *) value + sizeof(njs_value_t));
            value->data.u.string = string;

            string->start = (u_char *) string + sizeof(njs_string_t);
            string->length = src->data.u.string->length;
            string->retain = 0xffff;

            memcpy(string->start, start, size);
        }

        lhq.replace = 0;
        lhq.value = value;
        lhq.pool = vm->mem_cache_pool;

        ret = nxt_lvlhsh_insert(&parser->values_hash, &lhq);

        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_INDEX_NONE;
        }
    }

    if (start != (u_char *) src) {
        /*
         * The source node value must be updated with the shared value
         * allocated from the permanent memory pool because the node
         * value can be used as a variable initial value.
         */
        *(njs_value_t *) src = *value;
    }

    return (njs_index_t) value;
}
