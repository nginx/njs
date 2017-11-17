
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_utf8.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_pcre.h>
#include <nxt_malloc.h>
#include <nxt_string.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_object_hash.h>
#include <njs_array.h>
#include <njs_function.h>
#include <njs_error.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <njs_regexp.h>
#include <njs_regexp_pattern.h>
#include <string.h>


typedef struct {
    u_char                     *start;
    size_t                     size;
    njs_value_t                value;
} njs_string_replace_part_t;


#define NJS_SUBST_COPY        255
#define NJS_SUBST_PRECEDING   254
#define NJS_SUBST_FOLLOWING   253


typedef struct {
     uint32_t  type;
     uint32_t  size;
     u_char    *start;
} njs_string_subst_t;


typedef struct {
    union {
        njs_continuation_t     cont;
        u_char                 padding[NJS_CONTINUATION_SIZE];
    } u;
    /*
     * This retval value must be aligned so the continuation
     * is padded to aligned size.
     */
    njs_value_t                retval;

    nxt_array_t                parts;
    njs_string_replace_part_t  array[3];
    njs_string_replace_part_t  *part;

    nxt_array_t                *substitutions;
    njs_function_t             *function;

    nxt_regex_match_data_t     *match_data;

    njs_utf8_t                 utf8:8;
    njs_regexp_utf8_t          type:8;
} njs_string_replace_t;


static nxt_noinline void njs_string_slice_prop(njs_string_prop_t *string,
    njs_slice_prop_t *slice, njs_value_t *args, nxt_uint_t nargs);
static nxt_noinline void njs_string_slice_args(njs_slice_prop_t *slice,
    njs_value_t *args, nxt_uint_t nargs);
static njs_ret_t njs_string_from_char_code(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_string_starts_or_ends_with(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, nxt_bool_t starts);
static njs_ret_t njs_string_match_multiple(njs_vm_t *vm, njs_value_t *args,
    njs_regexp_pattern_t *pattern);
static njs_ret_t njs_string_split_part_add(njs_vm_t *vm, njs_array_t *array,
    njs_utf8_t utf8, u_char *start, size_t size);
static njs_ret_t njs_string_replace_regexp(njs_vm_t *vm, njs_value_t *args,
    njs_string_replace_t *r);
static njs_ret_t njs_string_replace_regexp_function(njs_vm_t *vm,
    njs_value_t *args, njs_string_replace_t *r, int *captures, nxt_uint_t n);
static njs_ret_t njs_string_replace_regexp_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_string_replace_regexp_join(njs_vm_t *vm,
    njs_string_replace_t *r);
static njs_ret_t njs_string_replace_search(njs_vm_t *vm, njs_value_t *args,
    njs_string_replace_t *r);
static njs_ret_t njs_string_replace_search_function(njs_vm_t *vm,
    njs_value_t *args, njs_string_replace_t *r);
static njs_ret_t njs_string_replace_search_continuation(njs_vm_t *vm,
    njs_value_t *args, nxt_uint_t nargs, njs_index_t unused);
static njs_ret_t njs_string_replace_parse(njs_vm_t *vm,
    njs_string_replace_t *r, u_char *p, u_char *end, size_t size,
    nxt_uint_t ncaptures);
static njs_ret_t njs_string_replace_substitute(njs_vm_t *vm,
    njs_string_replace_t *r, int *captures);
static njs_ret_t njs_string_replace_join(njs_vm_t *vm, njs_string_replace_t *r);
static void njs_string_replacement_copy(njs_string_replace_part_t *string,
    const njs_value_t *value);
static njs_ret_t njs_string_encode(njs_vm_t *vm, njs_value_t *value,
    const uint32_t *escape);
static njs_ret_t njs_string_decode(njs_vm_t *vm, njs_value_t *value,
    const uint32_t *reserve);


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


nxt_noinline njs_ret_t
njs_string_new(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size, uint32_t length)
{
    u_char  *p;

    p = njs_string_alloc(vm, value, size, length);

    if (nxt_fast_path(p != NULL)) {
        memcpy(p, start, size);
        return NXT_OK;
    }

    return NXT_ERROR;
}


nxt_noinline u_char *
njs_string_alloc(njs_vm_t *vm, njs_value_t *value, uint32_t size,
    uint32_t length)
{
    uint32_t      total, map_offset, *map;
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

    if (size != length && length > NJS_STRING_MAP_STRIDE) {
        map_offset = njs_string_map_offset(size);
        total = map_offset + njs_string_map_size(length);

    } else {
        map_offset = 0;
        total = size;
    }

    string = nxt_mem_cache_alloc(vm->mem_cache_pool,
                                 sizeof(njs_string_t) + total);

    if (nxt_fast_path(string != NULL)) {
        value->data.u.string = string;

        string->start = (u_char *) string + sizeof(njs_string_t);
        string->length = length;
        string->retain = 1;

        if (map_offset != 0) {
            map = (uint32_t *) (string->start + map_offset);
            map[0] = 0;
        }

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
 * sets njs_string_prop_t struct.
 */

nxt_noinline njs_ret_t
njs_string_validate(njs_vm_t *vm, njs_string_prop_t *string, njs_value_t *value)
{
    u_char    *start;
    size_t    new_size, map_offset;
    ssize_t   size, length;
    uint32_t  *map;

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

                if (length > NJS_STRING_MAP_STRIDE) {
                    /*
                     * Reallocate the long string with offset map
                     * after the string.
                     */
                    map_offset = njs_string_map_offset(size);
                    new_size = map_offset + njs_string_map_size(length);

                    start = nxt_mem_cache_alloc(vm->mem_cache_pool, new_size);
                    if (nxt_slow_path(start == NULL)) {
                        return NXT_ERROR;
                    }

                    memcpy(start, string->start, size);
                    string->start = start;
                    value->data.u.string->start = start;

                    map = (uint32_t *) (start + map_offset);
                    map[0] = 0;
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
njs_string_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_object_t       *object;
    const njs_value_t  *value;

    if (nargs == 1) {
        value = &njs_string_empty;

    } else {
        value = &args[1];
    }

    if (vm->top_frame->ctor) {
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


static const njs_object_prop_t  njs_string_constructor_properties[] =
{
    /* String.name == "String". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("String"),
    },

    /* String.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* String.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },

    /* String.fromCharCode(). */
    {
        .type = NJS_METHOD,
        .name = njs_string("fromCharCode"),
        .value = njs_native_function(njs_string_from_char_code, 0, 0),
    },

    /* String.fromCodePoint(), ECMAScript 6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("fromCodePoint"),
        .value = njs_native_function(njs_string_from_char_code, 0, 0),
    },
};


const njs_object_init_t  njs_string_constructor_init = {
    nxt_string("String"),
    njs_string_constructor_properties,
    nxt_nitems(njs_string_constructor_properties),
};


static njs_ret_t
njs_string_prototype_length(njs_vm_t *vm, njs_value_t *value)
{
    size_t     size;
    uintptr_t  length;

    /*
     * This getter can be called for string primitive, String object,
     * String.prototype.  The zero should be returned for the latter case.
     */
    length = 0;

    if (value->type == NJS_OBJECT_STRING) {
        value = &value->data.u.object_value->value;
    }

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
njs_string_prototype_value_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_STRING) {

        if (value->type == NJS_OBJECT_STRING) {
            value = &value->data.u.object_value->value;

        } else {
            njs_exception_type_error(vm, NULL, NULL);
            return NXT_ERROR;
        }
    }

    vm->retval = *value;

    return NXT_OK;
}

/*
 * String.concat(string2[, ..., stringN]).
 * JavaScript 1.2, ECMAScript 3.
 */

static njs_ret_t
njs_string_prototype_concat(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    u_char             *p, *start;
    size_t             size, length, mask;
    nxt_uint_t         i;
    njs_string_prop_t  string;

    if (njs_is_null_or_void(&args[0])) {
        njs_exception_type_error(vm, NULL, NULL);
        return NXT_ERROR;
    }

    for (i = 0; i < nargs; i++) {
        if (!njs_is_string(&args[i])) {
            njs_vm_trap_value(vm, &args[i]);

            return NJS_TRAP_STRING_ARG;
        }
    }

    if (nargs == 1) {
        njs_string_copy(&vm->retval, &args[0]);
        return NXT_OK;
    }

    size = 0;
    length = 0;
    mask = -1;

    for (i = 0; i < nargs; i++) {
        (void) njs_string_prop(&string, &args[i]);

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

    for (i = 0; i < nargs; i++) {
        (void) njs_string_prop(&string, &args[i]);

        p = memcpy(p, string.start, string.size);
        p += string.size;
    }

    return NXT_OK;
}


/*
 * String.fromUTF8(start[, end]).
 * The method converts an UTF-8 encoded byte string to an Unicode string.
 */

static njs_ret_t
njs_string_prototype_from_utf8(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    ssize_t            length;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_string_slice_prop(&string, &slice, args, nargs);

    if (string.length != 0) {
        /* ASCII or UTF8 string. */
        return njs_string_slice(vm, &vm->retval, &string, &slice);
    }

    string.start += slice.start;

    length = nxt_utf8_length(string.start, slice.length);

    if (length >= 0) {

        if (length < NJS_STRING_MAP_STRIDE || (size_t) length == slice.length) {
            /* ASCII or short UTF-8 string. */
            return njs_string_create(vm, &vm->retval, string.start,
                                     slice.length, length);
        }

        /* Long UTF-8 string. */
        return njs_string_new(vm, &vm->retval, string.start, slice.length,
                              length);
    }

    vm->retval = njs_value_null;

    return NXT_OK;
}


/*
 * String.toUTF8(start[, end]).
 * The method serializes Unicode string to an UTF-8 encoded byte string.
 */

static njs_ret_t
njs_string_prototype_to_utf8(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    (void) njs_string_prop(&string, &args[0]);

    string.length = 0;
    slice.string_length = string.size;

    njs_string_slice_args(&slice, args, nargs);

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


/*
 * String.fromBytes(start[, end]).
 * The method converts a byte string to an Unicode string.
 */

static njs_ret_t
njs_string_prototype_from_bytes(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    u_char             *p, *s, *start, *end;
    size_t             size;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_string_slice_prop(&string, &slice, args, nargs);

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
        }

        return NXT_OK;
    }

    return NXT_ERROR;
}


/*
 * String.toBytes(start[, end]).
 * The method serializes an Unicode string to a byte string.
 * The method returns null if a character larger than 255 is
 * encountered in the Unicode string.
 */

static njs_ret_t
njs_string_prototype_to_bytes(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    u_char             *p;
    size_t             length;
    uint32_t           byte;
    const u_char       *s, *end;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_string_slice_prop(&string, &slice, args, nargs);

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
njs_string_prototype_slice(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_string_slice_prop(&string, &slice, args, nargs);

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


/*
 * String.substring(start[, end]).
 * JavaScript 1.0, ECMAScript 1.
 */

static njs_ret_t
njs_string_prototype_substring(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    ssize_t            start, end, length;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    length = njs_string_prop(&string, &args[0]);

    slice.string_length = length;
    start = 0;

    if (nargs > 1) {
        start = args[1].data.u.number;

        if (start < 0) {
            start = 0;

        } else if (start > length) {
            start = length;
        }

        end = length;

        if (nargs > 2) {
            end = args[2].data.u.number;

            if (end < 0) {
                end = 0;

            } else if (end >= length) {
                end = length;
            }
        }

        length = end - start;

        if (length < 0) {
            length = -length;
            start = end;
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
njs_string_prototype_substr(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    ssize_t            start, length, n;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    length = njs_string_prop(&string, &args[0]);

    slice.string_length = length;
    start = 0;

    if (nargs > 1) {
        start = args[1].data.u.number;

        if (start < length) {
            if (start < 0) {
                start += length;

                if (start < 0) {
                    start = 0;
                }
            }

            length -= start;

            if (nargs > 2) {
                n = args[2].data.u.number;

                if (n < 0) {
                    length = 0;

                } else if (n < length) {
                    length = n;
                }
            }

        } else {
            start = 0;
            length = 0;
        }
    }

    slice.start = start;
    slice.length = length;

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


static njs_ret_t
njs_string_prototype_char_at(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    ssize_t            start, length;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    slice.string_length = njs_string_prop(&string, &args[0]);

    start = 0;
    length = 1;

    if (nargs > 1) {
        start = args[1].data.u.number;

        if (start < 0 || start >= (ssize_t) slice.string_length) {
            start = 0;
            length = 0;
        }
    }

    slice.start = start;
    slice.length = length;

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


static nxt_noinline void
njs_string_slice_prop(njs_string_prop_t *string, njs_slice_prop_t *slice,
    njs_value_t *args, nxt_uint_t nargs)
{
    slice->string_length = njs_string_prop(string, &args[0]);

    njs_string_slice_args(slice, args, nargs);
}


static nxt_noinline void
njs_string_slice_args(njs_slice_prop_t *slice, njs_value_t *args,
    nxt_uint_t nargs)
{
    ssize_t  start, end, length;

    length = slice->string_length;
    start = 0;

    if (nargs > 1) {
        start = args[1].data.u.number;

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
            end = length;

            if (nargs > 2) {
                end = args[2].data.u.number;

                if (end < 0) {
                    end += length;
                }
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
    }

    slice->start = start;
    slice->length = length;
}


nxt_noinline njs_ret_t
njs_string_slice(njs_vm_t *vm, njs_value_t *dst,
    const njs_string_prop_t *string, njs_slice_prop_t *slice)
{
    size_t        size, n, length;
    const u_char  *p, *start, *end;

    length = slice->length;
    start = string->start;

    if (string->size == slice->string_length) {
        /* Byte or ASCII string. */
        start += slice->start;
        size = slice->length;

    } else {
        /* UTF-8 string. */
        end = start + string->size;
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
        return njs_string_new(vm, &vm->retval, start, size, length);
    }

    vm->retval = njs_string_empty;

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_char_code_at(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double             num;
    ssize_t            index, length;
    uint32_t           code;
    const u_char       *start, *end;
    njs_string_prop_t  string;

    length = njs_string_prop(&string, &args[0]);

    index = 0;

    if (nargs > 1) {
        index = args[1].data.u.number;

        if (nxt_slow_path(index < 0 || index >= length)) {
            num = NAN;
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
njs_string_from_char_code(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    u_char      *p;
    double      num;
    size_t      size;
    int32_t     code;
    nxt_uint_t  i;

    for (i = 1; i < nargs; i++) {
        if (!njs_is_numeric(&args[i])) {
            njs_vm_trap_value(vm, &args[i]);
            return NJS_TRAP_NUMBER_ARG;
        }
    }

    size = 0;

    for (i = 1; i < nargs; i++) {
        num = args[i].data.u.number;
        if (isnan(num)) {
            goto range_error;
        }

        code = num;

        if (code != num || code < 0 || code >= 0x110000) {
            goto range_error;
        }

        size += nxt_utf8_size(code);
    }

    p = njs_string_alloc(vm, &vm->retval, size, nargs - 1);
    if (nxt_slow_path(p == NULL)) {
        return NXT_ERROR;
    }

    for (i = 1; i < nargs; i++) {
        p = nxt_utf8_encode(p, args[i].data.u.number);
    }

    return NXT_OK;

range_error:

    njs_exception_range_error(vm, NULL, NULL);

    return NXT_ERROR;
}


static njs_ret_t
njs_string_prototype_index_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    ssize_t            index, length, search_length;
    const u_char       *p, *end;
    njs_string_prop_t  string, search;

    if (nargs > 1) {
        length = njs_string_prop(&string, &args[0]);
        search_length = njs_string_prop(&search, &args[1]);

        index = 0;

        if (nargs > 2) {
            index = args[2].data.u.number;

            if (index < 0) {
                index = 0;
            }
        }

        if (length - index >= search_length) {
            end = string.start + string.size;

            if (string.size == (size_t) length) {
                /* Byte or ASCII string. */

                end -= (search.size - 1);

                for (p = string.start + index; p < end; p++) {
                    if (memcmp(p, search.start, search.size) == 0) {
                        goto done;
                    }

                    index++;
                }

            } else {
                /* UTF-8 string. */

                p = njs_string_offset(string.start, end, index);
                end -= search.size - 1;

                while (p < end) {
                    if (memcmp(p, search.start, search.size) == 0) {
                        goto done;
                    }

                    index++;
                    p = nxt_utf8_next(p, end);
                }
            }

        } else if (search.size == 0) {
            index = length;
            goto done;
        }
    }

    index = -1;

done:

    njs_number_set(&vm->retval, index);

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_last_index_of(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    ssize_t            index, start, length, search_length;
    const u_char       *p, *end;
    njs_string_prop_t  string, search;

    index = -1;

    if (nargs > 1) {
        length = njs_string_prop(&string, &args[0]);
        search_length = njs_string_prop(&search, &args[1]);

        if (length < search_length) {
            goto done;
        }

        index = NJS_STRING_MAX_LENGTH;

        if (nargs > 2) {
            index = args[2].data.u.number;

            if (index < 0) {
                index = 0;
            }
        }

        if (index > length) {
            index = length;
        }

        if (string.size == (size_t) length) {
            /* Byte or ASCII string. */

            start = length - search.size;

            if (index > start) {
                index = start;
            }

            p = string.start + index;

            do {
                if (memcmp(p, search.start, search.size) == 0) {
                    goto done;
                }

                index--;
                p--;

            } while (p >= string.start);

        } else {
            /* UTF-8 string. */

            end = string.start + string.size;
            p = njs_string_offset(string.start, end, index);
            end -= search.size;

            while (p > end) {
                index--;
                p = nxt_utf8_prev(p);
            }

            for ( ;; ) {
                if (memcmp(p, search.start, search.size) == 0) {
                    goto done;
                }

                index--;

                if (p <= string.start) {
                    break;
                }

                p = nxt_utf8_prev(p);
            }
        }
    }

done:

    njs_number_set(&vm->retval, index);

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_includes(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    ssize_t            index, length, search_length;
    const u_char       *p, *end;
    const njs_value_t  *retval;
    njs_string_prop_t  string, search;

    retval = &njs_value_true;

    if (nargs > 1) {
        search_length = njs_string_prop(&search, &args[1]);

        if (search_length == 0) {
            goto done;
        }

        length = njs_string_prop(&string, &args[0]);

        index = 0;

        if (nargs > 2) {
            index = args[2].data.u.number;

            if (index < 0) {
                index = 0;
            }
        }

        if (length - index >= search_length) {
            end = string.start + string.size;

            if (string.size == (size_t) length) {
                /* Byte or ASCII string. */
                p = string.start + index;

            } else {
                /* UTF-8 string. */
                p = njs_string_offset(string.start, end, index);
            }

            end -= search.size - 1;

            while (p < end) {
                if (memcmp(p, search.start, search.size) == 0) {
                    goto done;
                }

                p++;
            }
        }
    }

    retval = &njs_value_false;

done:

    vm->retval = *retval;

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_starts_with(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_string_starts_or_ends_with(vm, args, nargs, 1);
}


static njs_ret_t
njs_string_prototype_ends_with(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_string_starts_or_ends_with(vm, args, nargs, 0);
}


static njs_ret_t
njs_string_starts_or_ends_with(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, nxt_bool_t starts)
{
    ssize_t            index, length, search_length;
    const u_char       *p, *end;
    const njs_value_t  *retval;
    njs_string_prop_t  string, search;

    retval = &njs_value_true;

    if (nargs > 1) {
        search_length = njs_string_prop(&search, &args[1]);

        if (search_length == 0) {
            goto done;
        }

        length = njs_string_prop(&string, &args[0]);

        index = (nargs > 2) ? args[2].data.u.number : -1;

        if (starts) {
            if (index < 0) {
                index = 0;
            }

            if (length - index < search_length) {
                goto small;
            }

        } else {
            if (index < 0 || index > length) {
                index = length;
            }

            index -= search_length;

            if (index < 0) {
                goto small;
            }
        }

        end = string.start + string.size;

        if (string.size == (size_t) length) {
            /* Byte or ASCII string. */
            p = string.start + index;

        } else {
            /* UTF-8 string. */
            p = njs_string_offset(string.start, end, index);
        }

        if ((size_t) (end - p) >= search.size
            && memcmp(p, search.start, search.size) == 0)
        {
            goto done;
        }
    }

small:

    retval = &njs_value_false;

done:

    vm->retval = *retval;

    return NXT_OK;
}


/*
 * njs_string_offset() assumes that index is correct.
 */

nxt_noinline const u_char *
njs_string_offset(const u_char *start, const u_char *end, size_t index)
{
    uint32_t    *map;
    nxt_uint_t  skip;

    if (index >= NJS_STRING_MAP_STRIDE) {
        map = njs_string_map_start(end);

        if (map[0] == 0) {
            njs_string_offset_map_init(start, end - start);
        }

        start += map[index / NJS_STRING_MAP_STRIDE - 1];
    }

    for (skip = index % NJS_STRING_MAP_STRIDE; skip != 0; skip--) {
        start = nxt_utf8_next(start, end);
    }

    return start;
}


/*
 * njs_string_index() assumes that offset is correct.
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

    if (string->length >= NJS_STRING_MAP_STRIDE) {

        end = string->start + string->size;
        map = njs_string_map_start(end);

        if (map[0] == 0) {
            njs_string_offset_map_init(string->start, string->size);
        }

        while (index + NJS_STRING_MAP_STRIDE < string->length
               && *map <= offset)
        {
            last = *map++;
            index += NJS_STRING_MAP_STRIDE;
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


nxt_noinline void
njs_string_offset_map_init(const u_char *start, size_t size)
{
    size_t        offset;
    uint32_t      *map;
    nxt_uint_t    n;
    const u_char  *p, *end;

    end = start + size;
    map = njs_string_map_start(end);
    p = start;
    n = 0;
    offset = NJS_STRING_MAP_STRIDE;

    do {
        if (offset == 0) {
            map[n++] = p - start;
            offset = NJS_STRING_MAP_STRIDE;
        }

        /* The UTF-8 string should be valid since its length is known. */
        p = nxt_utf8_next(p, end);

        offset--;

    } while (p < end);
}


/*
 * String.toLowerCase().
 * The method supports only simple folding.  For example, Turkish "İ"
 * folding "\u0130" to "\u0069\u0307" is not supported.
 */

static njs_ret_t
njs_string_prototype_to_lower_case(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    size_t             size;
    u_char             *p, *start;
    const u_char       *s, *end;
    njs_string_prop_t  string;

    (void) njs_string_prop(&string, &args[0]);

    start = njs_string_alloc(vm, &vm->retval, string.size, string.length);
    if (nxt_slow_path(start == NULL)) {
        return NXT_ERROR;
    }

    p = start;
    s = string.start;
    size = string.size;

    if (string.length == 0 || string.length == size) {
        /* Byte or ASCII string. */

        while (size != 0) {
            *p++ = nxt_lower_case(*s++);
            size--;
        }

    } else {
        /* UTF-8 string. */
        end = s + size;

        while (size != 0) {
            p = nxt_utf8_encode(p, nxt_utf8_lower_case(&s, end));
            size--;
        }
    }

    return NXT_OK;
}


/*
 * String.toUpperCase().
 * The method supports only simple folding.  For example, German "ß"
 * folding "\u00DF" to "\u0053\u0053" is not supported.
 */

static njs_ret_t
njs_string_prototype_to_upper_case(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    size_t             size;
    u_char             *p, *start;
    const u_char       *s, *end;
    njs_string_prop_t  string;

    (void) njs_string_prop(&string, &args[0]);

    start = njs_string_alloc(vm, &vm->retval, string.size, string.length);
    if (nxt_slow_path(start == NULL)) {
        return NXT_ERROR;
    }

    p = start;
    s = string.start;
    size = string.size;

    if (string.length == 0 || string.length == size) {
        /* Byte or ASCII string. */

        while (size != 0) {
            *p++ = nxt_upper_case(*s++);
            size--;
        }

    } else {
        /* UTF-8 string. */
        end = s + size;

        while (size != 0) {
            p = nxt_utf8_encode(p, nxt_utf8_upper_case(&s, end));
            size--;
        }
    }

    return NXT_OK;
}


static njs_ret_t
njs_string_prototype_trim(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    uint32_t           u, trim, length;
    const u_char       *p, *prev, *start, *end;
    njs_string_prop_t  string;

    trim = 0;

    njs_string_prop(&string, &args[0]);

    p = string.start;
    end = string.start + string.size;

    if (string.length == 0 || string.length == string.size) {
        /* Byte or ASCII string. */

        while (p < end) {

            switch (*p) {
            case 0x09:  /* <TAB>  */
            case 0x0A:  /* <LF>   */
            case 0x0B:  /* <VT>   */
            case 0x0C:  /* <FF>   */
            case 0x0D:  /* <CR>   */
            case 0x20:  /* <SP>   */
            case 0xA0:  /* <NBSP> */
                trim++;
                p++;
                continue;

            default:
                start = p;
                p = end;

                for ( ;; ) {
                    p--;

                    switch (*p) {
                    case 0x09:  /* <TAB>  */
                    case 0x0A:  /* <LF>   */
                    case 0x0B:  /* <VT>   */
                    case 0x0C:  /* <FF>   */
                    case 0x0D:  /* <CR>   */
                    case 0x20:  /* <SP>   */
                    case 0xA0:  /* <NBSP> */
                        trim++;
                        continue;

                    default:
                        p++;
                        goto done;
                    }
                }
            }
        }

    } else {
        /* UTF-8 string. */

        while (p < end) {
            prev = p;
            u = nxt_utf8_decode(&p, end);

            switch (u) {
            case 0x0009:  /* <TAB>  */
            case 0x000A:  /* <LF>   */
            case 0x000B:  /* <VT>   */
            case 0x000C:  /* <FF>   */
            case 0x000D:  /* <CR>   */
            case 0x0020:  /* <SP>   */
            case 0x00A0:  /* <NBSP> */
            case 0x2028:  /* <LS>   */
            case 0x2029:  /* <PS>   */
            case 0xFEFF:  /* <BOM>  */
                trim++;
                continue;

            default:
                start = prev;
                prev = end;

                for ( ;; ) {
                    prev = nxt_utf8_prev(prev);
                    p = prev;
                    u = nxt_utf8_decode(&p, end);

                    switch (u) {
                    case 0x0009:  /* <TAB>  */
                    case 0x000A:  /* <LF>   */
                    case 0x000B:  /* <VT>   */
                    case 0x000C:  /* <FF>   */
                    case 0x000D:  /* <CR>   */
                    case 0x0020:  /* <SP>   */
                    case 0x00A0:  /* <NBSP> */
                    case 0x2028:  /* <LS>   */
                    case 0x2029:  /* <PS>   */
                    case 0xFEFF:  /* <BOM>  */
                        trim++;
                        continue;

                    default:
                        goto done;
                    }
                }
            }
        }
    }

    vm->retval = njs_string_empty;

    return NXT_OK;

done:

    if (trim == 0) {
        /* GC: retain. */
        vm->retval = args[0];

        return NXT_OK;
    }

    length = (string.length != 0) ? string.length - trim : 0;

    return njs_string_new(vm, &vm->retval, start, p - start, length);
}


static njs_ret_t
njs_string_prototype_repeat(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    u_char             *p, *start;
    int32_t            n, max;
    uint32_t           size, length;
    njs_string_prop_t  string;

    n = 0;

    (void) njs_string_prop(&string, &args[0]);

    if (nargs > 1) {
        max = (string.size > 1) ? NJS_STRING_MAX_LENGTH / string.size
                                : NJS_STRING_MAX_LENGTH;

        n = args[1].data.u.number;

        if (nxt_slow_path(n < 0 || n >= max)) {
            njs_exception_range_error(vm, NULL, NULL);
            return NXT_ERROR;
        }
    }

    if (string.size == 0) {
        vm->retval = njs_string_empty;
        return NXT_OK;
    }

    size = string.size * n;
    length = string.length * n;

    start = njs_string_alloc(vm, &vm->retval, size, length);
    if (nxt_slow_path(start == NULL)) {
        return NXT_ERROR;
    }

    p = start;

    while (n != 0) {
        p = memcpy(p, string.start, string.size);
        p += string.size;
        n--;
    }

    return NXT_OK;
}


/*
 * String.search([regexp])
 */

static njs_ret_t
njs_string_prototype_search(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    int                   ret, *captures;
    nxt_int_t             index;
    nxt_uint_t            n;
    njs_string_prop_t     string;
    njs_regexp_pattern_t  *pattern;

    index = 0;

    if (nargs > 1) {

        switch (args[1].type) {

        case NJS_REGEXP:
            pattern = args[1].data.u.regexp->pattern;
            break;

        case NJS_STRING:
            (void) njs_string_prop(&string, &args[1]);

            if (string.size != 0) {
                pattern = njs_regexp_pattern_create(vm, string.start,
                                                    string.size, 0);
                if (nxt_slow_path(pattern == NULL)) {
                    return NXT_ERROR;
                }

                break;
            }

            goto done;

        default:  /* NJS_VOID */
            goto done;
        }

        index = -1;

        (void) njs_string_prop(&string, &args[0]);

        n = (string.length != 0);

        if (nxt_regex_is_valid(&pattern->regex[n])) {
            ret = njs_regexp_match(vm, &pattern->regex[n], string.start,
                                   string.size, vm->single_match_data);
            if (ret >= 0) {
                captures = nxt_regex_captures(vm->single_match_data);
                index = njs_string_index(&string, captures[0]);

            } else if (ret != NXT_REGEX_NOMATCH) {
                return NXT_ERROR;
            }
        }
    }

done:

    njs_number_set(&vm->retval, index);

    return NXT_OK;
}


/*
 * String.match([regexp])
 */

static njs_ret_t
njs_string_prototype_match(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t             string;
    njs_ret_t             ret;
    njs_value_t           arguments[2];
    njs_regexp_pattern_t  *pattern;

    arguments[1] = args[0];

    string.start = NULL;
    string.length = 0;

    if (nargs > 1) {

        if (njs_is_regexp(&args[1])) {
            pattern = args[1].data.u.regexp->pattern;

            if (pattern->global) {
                return njs_string_match_multiple(vm, args, pattern);
            }

            /*
             * string.match(regexp) is the same as regexp.exec(string)
             * if the regexp has no global flag.
             */
            arguments[0] = args[1];

            goto match;
        }

        if (njs_is_string(&args[1])) {
            /* string1.match(string2) is the same as /string2/.exec(string1). */
            njs_string_get(&args[1], &string);
        }

        /* A void value. */
    }

    ret = njs_regexp_create(vm, &arguments[0], string.start, string.length, 0);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

match:

    return njs_regexp_prototype_exec(vm, arguments, nargs, unused);
}


static njs_ret_t
njs_string_match_multiple(njs_vm_t *vm, njs_value_t *args,
    njs_regexp_pattern_t *pattern)
{
    int                *captures;
    u_char             *start;
    int32_t            size, length;
    njs_ret_t          ret;
    njs_utf8_t         utf8;
    njs_array_t        *array;
    njs_regexp_utf8_t  type;
    njs_string_prop_t  string;

    args[1].data.u.regexp->last_index = 0;
    vm->retval = njs_value_null;

    (void) njs_string_prop(&string, &args[0]);

    utf8 = NJS_STRING_BYTE;
    type = NJS_REGEXP_BYTE;

    if (string.length != 0) {
        utf8 = NJS_STRING_ASCII;
        type = NJS_REGEXP_UTF8;

        if (string.length != string.size) {
            utf8 = NJS_STRING_UTF8;
        }
    }

    if (nxt_regex_is_valid(&pattern->regex[type])) {
        array = NULL;

        do {
            ret = njs_regexp_match(vm, &pattern->regex[type], string.start,
                                   string.size, vm->single_match_data);
            if (ret >= 0) {
                if (array != NULL) {
                    ret = njs_array_expand(vm, array, 0, 1);
                    if (nxt_slow_path(ret != NXT_OK)) {
                        return ret;
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

                captures = nxt_regex_captures(vm->single_match_data);
                start = &string.start[captures[0]];

                string.start += captures[1];
                string.size -= captures[1];

                size = captures[1] - captures[0];

                length = njs_string_length(utf8, start, size);

                ret = njs_string_create(vm, &array->start[array->length],
                                        start, size, length);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                array->length++;

            } else if (ret == NXT_REGEX_NOMATCH) {
                break;

            } else {
                return NXT_ERROR;
            }

        } while (string.size > 0);
    }

    return NXT_OK;
}


/*
 * String.split([string|regexp[, limit]])
 */

static njs_ret_t
njs_string_prototype_split(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    int                   ret, *captures;
    u_char                *p, *start, *next;
    size_t                size;
    uint32_t              limit;
    njs_utf8_t            utf8;
    njs_array_t           *array;
    const u_char          *end;
    njs_regexp_utf8_t     type;
    njs_string_prop_t     string, split;
    njs_regexp_pattern_t  *pattern;

    array = njs_array_alloc(vm, 0, NJS_ARRAY_SPARE);
    if (nxt_slow_path(array == NULL)) {
        return NXT_ERROR;
    }

    if (nargs > 1) {

        if (nargs > 2) {
            limit = args[2].data.u.number;

            if (limit == 0) {
                goto done;
            }

        } else {
            limit = (uint32_t) -1;
        }

        (void) njs_string_prop(&string, &args[0]);

        if (string.size == 0) {
            goto single;
        }

        utf8 = NJS_STRING_BYTE;
        type = NJS_REGEXP_BYTE;

        if (string.length != 0) {
            utf8 = NJS_STRING_ASCII;
            type = NJS_REGEXP_UTF8;

            if (string.length != string.size) {
                utf8 = NJS_STRING_UTF8;
            }
        }

        switch (args[1].type) {

        case NJS_STRING:
            (void) njs_string_prop(&split, &args[1]);

            if (string.size < split.size) {
                goto single;
            }

            start = string.start;
            end = string.start + string.size;

            do {
                for (p = start; p < end; p++) {
                    if (memcmp(p, split.start, split.size) == 0) {
                        break;
                    }
                }

                next = p + split.size;

                /* Empty split string. */
                if (p == next) {
                    p++;
                    next++;
                }

                size = p - start;

                ret = njs_string_split_part_add(vm, array, utf8, start, size);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                start = next;
                limit--;

            } while (limit != 0 && p < end);

            goto done;

        case NJS_REGEXP:
            pattern = args[1].data.u.regexp->pattern;

            if (!nxt_regex_is_valid(&pattern->regex[type])) {
                goto single;
            }

            start = string.start;
            end = string.start + string.size;

            do {
                ret = njs_regexp_match(vm, &pattern->regex[type], start,
                                       end - start, vm->single_match_data);
                if (ret >= 0) {
                    captures = nxt_regex_captures(vm->single_match_data);

                    p = start + captures[0];
                    next = start + captures[1];

                } else if (ret == NXT_REGEX_NOMATCH) {
                    p = (u_char *) end;
                    next = (u_char *) end + 1;

                } else {
                    return NXT_ERROR;
                }

                /* Empty split regexp. */
                if (p == next) {
                    p++;
                    next++;
                }

                size = p - start;

                ret = njs_string_split_part_add(vm, array, utf8, start, size);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                start = next;
                limit--;

            } while (limit != 0 && p < end);

            goto done;

        default: /* NJS_VOID */
            break;
        }
    }

single:

    /* GC: retain. */
    array->start[0] = args[0];
    array->length = 1;

done:

    vm->retval.data.u.array = array;
    vm->retval.type = NJS_ARRAY;
    vm->retval.data.truth = 1;

    return NXT_OK;
}


static njs_ret_t
njs_string_split_part_add(njs_vm_t *vm, njs_array_t *array, njs_utf8_t utf8,
    u_char *start, size_t size)
{
    ssize_t  length;

    length = njs_string_length(utf8, start, size);

    return njs_array_string_add(vm, array, start, size, length);
}


/*
 * String.replace([regexp|string[, string|function]])
 */

static njs_ret_t
njs_string_prototype_replace(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    u_char                *p, *start, *end;
    njs_ret_t             ret;
    nxt_uint_t            ncaptures;
    nxt_regex_t           *regex;
    njs_string_prop_t     string;
    njs_string_replace_t  *r;

    if (nargs == 1) {
        goto original;
    }

    (void) njs_string_prop(&string, &args[0]);

    if (string.size == 0) {
        goto original;
    }

    r = njs_vm_continuation(vm);

    r->utf8 = NJS_STRING_BYTE;
    r->type = NJS_REGEXP_BYTE;

    if (string.length != 0) {
        r->utf8 = NJS_STRING_ASCII;
        r->type = NJS_REGEXP_UTF8;

        if (string.length != string.size) {
            r->utf8 = NJS_STRING_UTF8;
        }
    }

    if (njs_is_regexp(&args[1])) {
        regex = &args[1].data.u.regexp->pattern->regex[r->type];

        if (!nxt_regex_is_valid(regex)) {
            goto original;
        }

        ncaptures = nxt_regex_ncaptures(regex);

    } else {
        regex = NULL;
        ncaptures = 1;
    }

    /* This cannot fail. */
    r->part = nxt_array_init(&r->parts, &r->array,
                             3, sizeof(njs_string_replace_part_t),
                             &njs_array_mem_proto, vm->mem_cache_pool);

    r->substitutions = NULL;
    r->function = NULL;

    /* A literal replacement is stored in the second part. */

    if (nargs == 2) {
        njs_string_replacement_copy(&r->part[1], &njs_string_void);

    } else if (njs_is_string(&args[2])) {
        njs_string_replacement_copy(&r->part[1], &args[2]);

        start = r->part[1].start;

        if (start == NULL) {
            start = r->part[1].value.short_string.start;
        }

        end = start + r->part[1].size;

        for (p = start; p < end; p++) {
            if (*p == '$') {
                ret = njs_string_replace_parse(vm, r, p, end, p - start,
                                               ncaptures);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                /* Reset parts array to the subject string only. */
                r->parts.items = 1;

                break;
            }
        }

    } else {
        r->function = args[2].data.u.function;
    }

    r->part[0].start = string.start;
    r->part[0].size = string.size;
    njs_set_invalid(&r->part[0].value);

    if (regex != NULL) {
        r->match_data = nxt_regex_match_data(regex, vm->regex_context);
        if (nxt_slow_path(r->match_data == NULL)) {
            return NXT_ERROR;
        }

        return njs_string_replace_regexp(vm, args, r);
    }

    return njs_string_replace_search(vm, args, r);

original:

    njs_string_copy(&vm->retval, &args[0]);

    return NXT_OK;
}


static njs_ret_t
njs_string_replace_regexp(njs_vm_t *vm, njs_value_t *args,
    njs_string_replace_t *r)
{
    int                        *captures;
    njs_ret_t                  ret;
    njs_regexp_pattern_t       *pattern;
    njs_string_replace_part_t  *part;

    pattern = args[1].data.u.regexp->pattern;

    do {
        ret = njs_regexp_match(vm, &pattern->regex[r->type],
                               r->part[0].start, r->part[0].size,
                               r->match_data);

        if (ret >= 0) {
            captures = nxt_regex_captures(r->match_data);

            if (r->substitutions != NULL) {
                ret = njs_string_replace_substitute(vm, r, captures);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

                if (!pattern->global) {
                    return njs_string_replace_regexp_join(vm, r);
                }

            } else {
                if (r->part != r->parts.start) {
                    r->part = nxt_array_add(&r->parts, &njs_array_mem_proto,
                                            vm->mem_cache_pool);
                    if (nxt_slow_path(r->part == NULL)) {
                        return NXT_ERROR;
                    }

                    r->part = nxt_array_add(&r->parts, &njs_array_mem_proto,
                                            vm->mem_cache_pool);
                    if (nxt_slow_path(r->part == NULL)) {
                        return NXT_ERROR;
                    }

                    r->part -= 2;
                }

                r->part[2].start = r->part[0].start + captures[1];
                r->part[2].size = r->part[0].size - captures[1];
                njs_set_invalid(&r->part[2].value);

                if (r->function != NULL) {
                    return njs_string_replace_regexp_function(vm, args, r,
                                                              captures, ret);
                }

                r->part[0].size = captures[0];

                if (!pattern->global) {
                    return njs_string_replace_regexp_join(vm, r);
                }

                /* A literal replacement is stored in the second part. */
                part = r->parts.start;
                r->part[1] = part[1];

                r->part += 2;
            }

        } else if (ret == NXT_REGEX_NOMATCH) {
            break;

        } else {
            return NXT_ERROR;
        }

    } while (r->part[0].size > 0);

    if (r->part != r->parts.start) {
        return njs_string_replace_regexp_join(vm, r);
    }

    nxt_regex_match_data_free(r->match_data, vm->regex_context);

    nxt_array_destroy(&r->parts, &njs_array_mem_proto, vm->mem_cache_pool);

    njs_string_copy(&vm->retval, &args[0]);

    return NXT_OK;
}


static njs_ret_t
njs_string_replace_regexp_function(njs_vm_t *vm, njs_value_t *args,
    njs_string_replace_t *r, int *captures, nxt_uint_t n)
{
    u_char       *start;
    size_t       size, length;
    njs_ret_t    ret;
    nxt_uint_t   i, k;
    njs_value_t  *arguments;

    r->u.cont.function = njs_string_replace_regexp_continuation;
    njs_set_invalid(&r->retval);

    arguments = nxt_mem_cache_alloc(vm->mem_cache_pool,
                                    (n + 3) * sizeof(njs_value_t));
    if (nxt_slow_path(arguments == NULL)) {
        return NXT_ERROR;
    }

    arguments[0] = njs_value_void;

    /* Matched substring and parenthesized submatch strings. */
    for (k = 0, i = 1; i <= n; i++) {

        start = r->part[0].start + captures[k];
        size = captures[k + 1] - captures[k];
        k += 2;

        length = njs_string_length(r->utf8, start, size);

        ret = njs_string_create(vm, &arguments[i], start, size, length);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    /* The offset of the matched substring. */
    njs_number_set(&arguments[n + 1], captures[0]);

    /* The whole string being examined. */
    length = njs_string_length(r->utf8, r->part[0].start, r->part[0].size);

    ret = njs_string_create(vm, &arguments[n + 2], r->part[0].start,
                            r->part[0].size, length);

    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    r->part[0].size = captures[0];

    return njs_function_apply(vm, r->function, arguments, n + 3,
                              (njs_index_t) &r->retval);
}


static njs_ret_t
njs_string_replace_regexp_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_string_replace_t  *r;

    r = njs_vm_continuation(vm);

    if (njs_is_string(&r->retval)) {
        njs_string_replacement_copy(&r->part[1], &r->retval);

        if (args[1].data.u.regexp->pattern->global) {
            r->part += 2;
            return njs_string_replace_regexp(vm, args, r);
        }

        return njs_string_replace_regexp_join(vm, r);
    }

    nxt_regex_match_data_free(r->match_data, vm->regex_context);

    njs_exception_type_error(vm, NULL, NULL);

    return NXT_ERROR;
}


static njs_ret_t
njs_string_replace_regexp_join(njs_vm_t *vm, njs_string_replace_t *r)
{
    nxt_regex_match_data_free(r->match_data, vm->regex_context);

    return njs_string_replace_join(vm, r);
}


static njs_ret_t
njs_string_replace_search(njs_vm_t *vm, njs_value_t *args,
    njs_string_replace_t *r)
{
    int        captures[2];
    u_char     *p, *end;
    size_t     size;
    njs_ret_t  ret;
    nxt_str_t  search;

    njs_string_get(&args[1], &search);

    p = r->part[0].start;
    end = (p + r->part[0].size) - (search.length - 1);

    do {
        if (memcmp(p, search.start, search.length) == 0) {

            if (r->substitutions != NULL) {
                captures[0] = p - r->part[0].start;
                captures[1] = captures[0] + search.length;

                ret = njs_string_replace_substitute(vm, r, captures);
                if (nxt_slow_path(ret != NXT_OK)) {
                    return ret;
                }

            } else {
                r->part[2].start = p + search.length;
                size = p - r->part[0].start;
                r->part[2].size = r->part[0].size - size - search.length;
                r->part[0].size = size;
                njs_set_invalid(&r->part[2].value);

                if (r->function != NULL) {
                    return njs_string_replace_search_function(vm, args, r);
                }
            }

            return njs_string_replace_join(vm, r);
        }

        if (r->utf8 < 2) {
            p++;

        } else {
            p = (u_char *) nxt_utf8_next(p, end);
        }

    } while (p < end);

    njs_string_copy(&vm->retval, &args[0]);

    return NXT_OK;
}


static njs_ret_t
njs_string_replace_search_function(njs_vm_t *vm, njs_value_t *args,
    njs_string_replace_t *r)
{
    njs_value_t  arguments[4];

    r->u.cont.function = njs_string_replace_search_continuation;

    arguments[0] = njs_value_void;

    /* GC, args[0], args[1] */

    /* Matched substring, it is the same as the args[1]. */
    arguments[1] = args[1];

    /* The offset of the matched substring. */
    njs_number_set(&arguments[2], r->part[0].size);

    /* The whole string being examined. */
    arguments[3] = args[0];

    return njs_function_apply(vm, r->function, arguments, 4,
                              (njs_index_t) &r->retval);
}


static njs_ret_t
njs_string_replace_search_continuation(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    njs_string_replace_t  *r;

    r = njs_vm_continuation(vm);

    if (njs_is_string(&r->retval)) {
        njs_string_replacement_copy(&r->part[1], &r->retval);

        return njs_string_replace_join(vm, r);
    }

    njs_exception_type_error(vm, NULL, NULL);

    return NXT_ERROR;
}


static njs_ret_t
njs_string_replace_parse(njs_vm_t *vm, njs_string_replace_t *r, u_char *p,
    u_char *end, size_t size, nxt_uint_t ncaptures)
{
    u_char              c;
    uint32_t            type;
    njs_string_subst_t  *s;

    r->substitutions = nxt_array_create(4, sizeof(njs_string_subst_t),
                                     &njs_array_mem_proto, vm->mem_cache_pool);

    if (nxt_slow_path(r->substitutions == NULL)) {
        return NXT_ERROR;
    }

    s = NULL;

    if (size == 0) {
        goto skip;
    }

copy:

    if (s == NULL) {
        s = nxt_array_add(r->substitutions, &njs_array_mem_proto,
                          vm->mem_cache_pool);
        if (nxt_slow_path(s == NULL)) {
            return NXT_ERROR;
        }

        s->type = NJS_SUBST_COPY;
        s->size = size;
        s->start = p - size;

    } else {
        s->size += size;
    }

skip:

    while (p < end) {
        size = 1;
        c = *p++;

        if (c != '$' || p == end) {
            goto copy;
        }

        c = *p++;

        if (c == '$') {
            s = NULL;
            goto copy;
        }

        size = 2;

        if (c >= '0' && c <= '9') {
            type = c - '0';

            if (p < end) {
                c = *p;

                if (c >= '0' && c <= '9') {
                    type = type * 10 + (c - '0');
                    p++;
                    size = 3;
                }
            }

            if (type >= ncaptures) {
                goto copy;
            }

            type *= 2;

        } else if (c == '&') {
            type = 0;

        } else if (c == '`') {
            type = NJS_SUBST_PRECEDING;

        } else if (c == '\'') {
            type = NJS_SUBST_FOLLOWING;

        } else {
            goto copy;
        }

        s = nxt_array_add(r->substitutions, &njs_array_mem_proto,
                          vm->mem_cache_pool);
        if (nxt_slow_path(s == NULL)) {
            return NXT_ERROR;
        }

        s->type = type;
        s = NULL;
    }

    return NXT_OK;
}


static njs_ret_t
njs_string_replace_substitute(njs_vm_t *vm, njs_string_replace_t *r,
    int *captures)
{
    uint32_t                   i, n, last;
    njs_string_subst_t         *s;
    njs_string_replace_part_t  *part, *subject;

    last = r->substitutions->items;

    part = nxt_array_add_multiple(&r->parts, &njs_array_mem_proto,
                                  vm->mem_cache_pool, last + 1);
    if (nxt_slow_path(part == NULL)) {
        return NXT_ERROR;
    }

    r->part = &part[-1];

    part[last].start = r->part[0].start + captures[1];
    part[last].size = r->part[0].size - captures[1];
    njs_set_invalid(&part[last].value);

    r->part[0].size = captures[0];

    s = r->substitutions->start;

    for (i = 0; i < last; i++) {
        n = s[i].type;

        switch (n) {

        /* Literal text, "$$", and out of range "$n" substitutions. */
        case NJS_SUBST_COPY:
            part->start = s[i].start;
            part->size = s[i].size;
            break;

        /* "$`" substitution. */
        case NJS_SUBST_PRECEDING:
            subject = r->parts.start;
            part->start = subject->start;
            part->size = (r->part[0].start - subject->start) + r->part[0].size;
            break;

        /* "$'" substitution. */
        case NJS_SUBST_FOLLOWING:
            part->start = r->part[last + 1].start;
            part->size = r->part[last + 1].size;
            break;

        /*
         * "$n" substitutions.
         * "$&" is the same as "$0", the "$0" however is not supported.
         */
        default:
            part->start = r->part[0].start + captures[n];
            part->size = captures[n + 1] - captures[n];
            break;
        }

        njs_set_invalid(&part->value);
        part++;
    }

    r->part = part;

    return NXT_OK;
}


static njs_ret_t
njs_string_replace_join(njs_vm_t *vm, njs_string_replace_t *r)
{
    u_char                     *p, *string;
    size_t                     size, length, mask;
    ssize_t                    len;
    nxt_uint_t                 i, n;
    njs_string_replace_part_t  *part;

    size = 0;
    length = 0;
    mask = -1;

    part = r->parts.start;
    n = r->parts.items;

    for (i = 0; i < n; i++) {
        if (part[i].start == NULL) {
            part[i].start = part[i].value.short_string.start;
        }

        size += part[i].size;

        len = nxt_utf8_length(part[i].start, part[i].size);

        if (len >= 0) {
            length += len;

        } else {
            mask = 0;
        }
    }

    length &= mask;

    string = njs_string_alloc(vm, &vm->retval, size, length);
    if (nxt_slow_path(string == NULL)) {
        return NXT_ERROR;
    }

    p = string;

    for (i = 0; i < n; i++) {
        p = memcpy(p, part[i].start, part[i].size);
        p += part[i].size;

        /* GC: release valid values. */
    }

    nxt_array_destroy(&r->parts, &njs_array_mem_proto, vm->mem_cache_pool);

    return NXT_OK;
}


static void
njs_string_replacement_copy(njs_string_replace_part_t *string,
    const njs_value_t *value)
{
    size_t  size;

    string->value = *value;

    size = value->short_string.size;

    if (size != NJS_STRING_LONG) {
        string->start = NULL;

    } else {
        string->start = value->data.u.string->start;
        size = value->data.string_size;
    }

    string->size = size;
}


njs_ret_t
njs_primitive_value_to_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *src)
{
    const njs_value_t  *value;

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

    default:
        return NXT_ERROR;
    }

    *dst = *value;

    return NXT_OK;
}


double
njs_string_to_number(njs_value_t *value, nxt_bool_t parse_float)
{
    u_char      *p, *start, *end;
    double      num;
    size_t      size;
    nxt_bool_t  minus;

    const size_t  infinity = sizeof("Infinity") - 1;

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

    if (p == end) {
        return NAN;
    }

    if (!parse_float
        && p + 2 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        p += 2;
        num = njs_number_hex_parse(&p, end);

    } else {
        start = p;
        num = njs_number_dec_parse(&p, end);

        if (p == start) {
            if (p + infinity > end || memcmp(p, "Infinity", infinity) != 0) {
                return NAN;
            }

            num = INFINITY;
            p += infinity;
        }
    }

    if (!parse_float) {
        while (p < end) {
            if (*p != ' ' && *p != '\t') {
                return NAN;
            }

            p++;
        }
    }

    return minus ? -num : num;
}


double
njs_string_to_index(njs_value_t *value)
{
    u_char  *p, *end;
    double  num;
    size_t  size;

    size = value->short_string.size;

    if (size != NJS_STRING_LONG) {
        p = value->short_string.start;

    } else {
        size = value->data.string_size;
        p = value->data.u.string->start;
    }

    if (size == 0) {
        return NAN;
    }

    if (*p == '0' && size > 1) {
        return NAN;
    }

    end = p + size;
    num = njs_number_dec_parse(&p, end);

    if (p != end) {
        return NAN;
    }

    return num;
}


/*
 * If string value is null-terminated the corresponding C string
 * is returned as is, otherwise the new copy is allocated with
 * the terminating zero byte.
 */
u_char *
njs_string_to_c_string(njs_vm_t *vm, njs_value_t *value)
{
    u_char  *p, *data, *start;
    size_t  size;

    if (value->short_string.size != NJS_STRING_LONG) {
        start = value->short_string.start;
        size = value->short_string.size;

        if (start[size] == '\0') {
            return start;

        } else if (size < NJS_STRING_SHORT) {
            start[size] = '\0';
            return start;
        }

    } else {
        start = value->data.u.string->start;
        size = value->data.string_size;

        if (start[size] == '\0') {
            return start;
        }
    }

    data = nxt_mem_cache_alloc(vm->mem_cache_pool, size + 1);
    if (nxt_slow_path(data == NULL)) {
        njs_exception_memory_error(vm);
        return NULL;
    }

    p = nxt_cpymem(data, start, size);
    *p++ = '\0';

    return data;
}


static const njs_object_prop_t  njs_string_prototype_properties[] =
{
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("__proto__"),
        .value = njs_native_getter(njs_primitive_prototype_get_proto),
    },

    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("length"),
        .value = njs_native_getter(njs_string_prototype_length),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_string_prototype_value_of, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_string_prototype_value_of, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("concat"),
        .value = njs_native_function(njs_string_prototype_concat, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("fromUTF8"),
        .value = njs_native_function(njs_string_prototype_from_utf8, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toUTF8"),
        .value = njs_native_function(njs_string_prototype_to_utf8, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("fromBytes"),
        .value = njs_native_function(njs_string_prototype_from_bytes, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toBytes"),
        .value = njs_native_function(njs_string_prototype_to_bytes, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("slice"),
        .value = njs_native_function(njs_string_prototype_slice, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("substring"),
        .value = njs_native_function(njs_string_prototype_substring, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("substr"),
        .value = njs_native_function(njs_string_prototype_substr, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("charAt"),
        .value = njs_native_function(njs_string_prototype_char_at, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("charCodeAt"),
        .value = njs_native_function(njs_string_prototype_char_code_at, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG),
    },

    /* String.codePointAt(), ECMAScript 6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("codePointAt"),
        .value = njs_native_function(njs_string_prototype_char_code_at, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("indexOf"),
        .value = njs_native_function(njs_string_prototype_index_of, 0,
                     NJS_STRING_OBJECT_ARG, NJS_STRING_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("lastIndexOf"),
        .value = njs_native_function(njs_string_prototype_last_index_of, 0,
                     NJS_STRING_OBJECT_ARG, NJS_STRING_ARG, NJS_INTEGER_ARG),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("includes"),
        .value = njs_native_function(njs_string_prototype_includes, 0,
                     NJS_STRING_OBJECT_ARG, NJS_STRING_ARG, NJS_INTEGER_ARG),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("startsWith"),
        .value = njs_native_function(njs_string_prototype_starts_with, 0,
                     NJS_STRING_OBJECT_ARG, NJS_STRING_ARG, NJS_INTEGER_ARG),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("endsWith"),
        .value = njs_native_function(njs_string_prototype_ends_with, 0,
                     NJS_STRING_OBJECT_ARG, NJS_STRING_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toLowerCase"),
        .value = njs_native_function(njs_string_prototype_to_lower_case, 0,
                     NJS_STRING_OBJECT_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toUpperCase"),
        .value = njs_native_function(njs_string_prototype_to_upper_case, 0,
                     NJS_STRING_OBJECT_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("trim"),
        .value = njs_native_function(njs_string_prototype_trim, 0,
                     NJS_STRING_OBJECT_ARG),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("repeat"),
        .value = njs_native_function(njs_string_prototype_repeat, 0,
                     NJS_STRING_OBJECT_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("search"),
        .value = njs_native_function(njs_string_prototype_search, 0,
                     NJS_STRING_OBJECT_ARG, NJS_REGEXP_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("match"),
        .value = njs_native_function(njs_string_prototype_match, 0,
                     NJS_STRING_OBJECT_ARG, NJS_REGEXP_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("split"),
        .value = njs_native_function(njs_string_prototype_split, 0,
                     NJS_STRING_OBJECT_ARG, NJS_REGEXP_ARG, NJS_INTEGER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("replace"),
        .value = njs_native_function(njs_string_prototype_replace,
                     njs_continuation_size(njs_string_replace_t),
                     NJS_STRING_OBJECT_ARG, NJS_REGEXP_ARG, NJS_FUNCTION_ARG),
    },
};


const njs_object_init_t  njs_string_prototype_init = {
    nxt_string("String"),
    njs_string_prototype_properties,
    nxt_nitems(njs_string_prototype_properties),
};


/*
 * encodeURI(string)
 */

njs_ret_t
njs_string_encode_uri(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    static const uint32_t  escape[] = {
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */

                     /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
        0x50000025,  /* 0101 0000 0000 0000  0000 0000 0010 0101 */

                     /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
        0x78000000,  /* 0111 1000 0000 0000  0000 0000 0000 0000 */

                     /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
        0xb8000001,  /* 1011 1000 0000 0000  0000 0000 0000 0001 */

        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
    };

    if (nargs > 1) {
        return njs_string_encode(vm, &args[1], escape);
    }

    vm->retval = njs_string_void;

    return NXT_OK;
}


/*
 * encodeURIComponent(string)
 */

njs_ret_t
njs_string_encode_uri_component(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    static const uint32_t  escape[] = {
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */

                     /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
        0xfc00987d,  /* 1111 1100 0000 0000  1001 1000 0111 1101 */

                     /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
        0x78000001,  /* 0111 1000 0000 0000  0000 0000 0000 0001 */

                     /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
        0xb8000001,  /* 1011 1000 0000 0000  0000 0000 0000 0001 */

        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
    };

    if (nargs > 1) {
        return njs_string_encode(vm, &args[1], escape);
    }

    vm->retval = njs_string_void;

    return NXT_OK;
}


static njs_ret_t
njs_string_encode(njs_vm_t *vm, njs_value_t *value, const uint32_t *escape)
{
    u_char               byte, *src, *dst;
    size_t               n, size;
    nxt_str_t            string;
    static const u_char  hex[16] = "0123456789ABCDEF";

    nxt_prefetch(escape);

    njs_string_get(value, &string);

    src = string.start;
    n = 0;

    for (size = string.length; size != 0; size--) {
        byte = *src++;

        if ((escape[byte >> 5] & ((uint32_t) 1 << (byte & 0x1f))) != 0) {
            n += 2;
        }
    }

    if (n == 0) {
        /* GC: retain src. */
        vm->retval = *value;
        return NXT_OK;
    }

    size = string.length + n;

    dst = njs_string_alloc(vm, &vm->retval, size, size);
    if (nxt_slow_path(dst == NULL)) {
        return NXT_ERROR;
    }

    size = string.length;
    src = string.start;

    do {
        byte = *src++;

        if ((escape[byte >> 5] & ((uint32_t) 1 << (byte & 0x1f))) != 0) {
            *dst++ = '%';
            *dst++ = hex[byte >> 4];
            *dst++ = hex[byte & 0xf];

        } else {
            *dst++ = byte;
        }

        size--;

    } while (size != 0);

    return NXT_OK;
}


/*
 * decodeURI(string)
 */

njs_ret_t
njs_string_decode_uri(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    static const uint32_t  reserve[] = {
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */

                     /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
        0xac009858,  /* 1010 1100 0000 0000  1001 1000 0101 1000 */

                     /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
        0x00000001,  /* 0000 0000 0000 0000  0000 0000 0000 0001 */

                     /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */

        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
    };

    if (nargs > 1) {
        return njs_string_decode(vm, &args[1], reserve);
    }

    vm->retval = njs_string_void;

    return NXT_OK;
}


/*
 * decodeURIComponent(string)
 */

njs_ret_t
njs_string_decode_uri_component(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    static const uint32_t  reserve[] = {
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */

                     /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */

                     /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */

                     /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */

        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
        0x00000000,  /* 0000 0000 0000 0000  0000 0000 0000 0000 */
    };

    if (nargs > 1) {
        return njs_string_decode(vm, &args[1], reserve);
    }

    vm->retval = njs_string_void;

    return NXT_OK;
}


static njs_ret_t
njs_string_decode(njs_vm_t *vm, njs_value_t *value, const uint32_t *reserve)
{
    int8_t               d0, d1;
    u_char               byte, *start, *src, *dst;
    size_t               n;
    ssize_t              size, length;
    nxt_str_t            string;
    nxt_bool_t           utf8;

    static const int8_t  hex[256]
        nxt_aligned(32) =
    {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    nxt_prefetch(&hex['0']);
    nxt_prefetch(reserve);

    njs_string_get(value, &string);

    src = string.start;
    n = 0;

    for (size = string.length; size != 0; size--) {
        byte = *src++;

        if (byte == '%') {
            size -= 2;

            if (size <= 0) {
                goto uri_error;
            }

            d0 = hex[*src++];
            if (d0 < 0) {
                goto uri_error;
            }

            d1 = hex[*src++];
            if (d1 < 0) {
                goto uri_error;
            }

            byte = (d0 << 4) + d1;

            if ((reserve[byte >> 5] & ((uint32_t) 1 << (byte & 0x1f))) == 0) {
                n += 2;
            }
        }
    }

    if (n == 0) {
        /* GC: retain src. */
        vm->retval = *value;
        return NXT_OK;
    }

    n = string.length - n;

    start = njs_string_alloc(vm, &vm->retval, n, n);
    if (nxt_slow_path(start == NULL)) {
        return NXT_ERROR;
    }

    utf8 = 0;
    dst = start;
    size = string.length;
    src = string.start;

    do {
        byte = *src++;

        if (byte == '%') {
            size -= 2;

            d0 = hex[*src++];
            d1 = hex[*src++];
            byte = (d0 << 4) + d1;

            utf8 |= (byte >= 0x80);

            if ((reserve[byte >> 5] & ((uint32_t) 1 << (byte & 0x1f))) != 0) {
                *dst++ = '%';
                *dst++ = src[-2];
                byte = src[-1];
            }
        }

        *dst++ = byte;
        size--;

    } while (size != 0);

    if (utf8) {
        length = nxt_utf8_length(start, n);

        if (length < 0) {
            length = 0;
        }

        if (vm->retval.short_string.size != NJS_STRING_LONG) {
            vm->retval.short_string.length = length;

        } else {
            vm->retval.data.u.string->length = length;
        }
    }

    return NXT_OK;

uri_error:

    njs_exception_uri_error(vm, NULL, NULL);

    return NXT_ERROR;
}


static nxt_int_t
njs_values_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_value_t  *value;

    value = data;

    if (lhq->key.length == sizeof(njs_value_t)
        && memcmp(lhq->key.start, value, sizeof(njs_value_t)) == 0)
    {
        return NXT_OK;
    }

    if (njs_is_string(value)
        && value->data.string_size == lhq->key.length
        && memcmp(value->data.u.string->start, lhq->key.start, lhq->key.length)
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
 * Constant values such as njs_value_true are copied to values_hash during
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
    nxt_lvlhsh_t        *values_hash;
    nxt_lvlhsh_query_t  lhq;

    if (src->type != NJS_STRING || src->short_string.size != NJS_STRING_LONG) {
        size = sizeof(njs_value_t);
        start = (u_char *) src;

    } else {
        size = src->data.string_size;
        start = src->data.u.string->start;
    }

    lhq.key_hash = nxt_djb_hash(start, size);
    lhq.key.length = size;
    lhq.key.start = start;
    lhq.proto = &njs_values_hash_proto;

    if (nxt_lvlhsh_find(&vm->shared->values_hash, &lhq) == NXT_OK) {
        value = lhq.value;

    } else if (parser->runtime
               && nxt_lvlhsh_find(&vm->values_hash, &lhq) == NXT_OK)
    {
        value = lhq.value;

    } else {
        value_size = 0;

        if (start != (u_char *) src) {
            /* Long string value is allocated together with string. */
            value_size = sizeof(njs_value_t) + sizeof(njs_string_t);

            length = src->data.u.string->length;

            if (size != length && length > NJS_STRING_MAP_STRIDE) {
                size = njs_string_map_offset(size)
                       + njs_string_map_size(length);
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

        values_hash = parser->runtime ? &vm->values_hash
                                      : &vm->shared->values_hash;

        ret = nxt_lvlhsh_insert(values_hash, &lhq);

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
