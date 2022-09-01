
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static u_char   njs_basis64[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
};


static u_char   njs_basis64url[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 63,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
};


static u_char   njs_basis64_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static u_char   njs_basis64url_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";


static void njs_encode_base64_core(njs_str_t *dst, const njs_str_t *src,
    const u_char *basis, njs_uint_t padding);
static njs_int_t njs_string_decode_base64_core(njs_vm_t *vm,
    njs_value_t *value, const njs_str_t *src, njs_bool_t url);
static njs_int_t njs_string_slice_prop(njs_vm_t *vm, njs_string_prop_t *string,
    njs_slice_prop_t *slice, njs_value_t *args, njs_uint_t nargs);
static njs_int_t njs_string_slice_args(njs_vm_t *vm, njs_slice_prop_t *slice,
    njs_value_t *args, njs_uint_t nargs);
static njs_int_t njs_string_from_char_code(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t is_point);
static njs_int_t njs_string_bytes_from(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_string_bytes_from_array_like(njs_vm_t *vm,
    njs_value_t *value);
static njs_int_t njs_string_bytes_from_string(njs_vm_t *vm,
    const njs_value_t *string, const njs_value_t *encoding);
static njs_int_t njs_string_match_multiple(njs_vm_t *vm, njs_value_t *args,
    njs_regexp_pattern_t *pattern);


#define njs_base64_encoded_length(len)       (((len + 2) / 3) * 4)
#define njs_base64_decoded_length(len, pad)  (((len / 4) * 3) - pad)


njs_int_t
njs_string_set(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size)
{
    u_char        *dst;
    const u_char  *src;
    njs_string_t  *string;

    value->type = NJS_STRING;
    njs_string_truth(value, size);

    if (size <= NJS_STRING_SHORT) {
        value->short_string.size = size;
        value->short_string.length = 0;

        dst = value->short_string.start;
        src = start;

        while (size != 0) {
            /* The maximum size is just 14 bytes. */
            njs_pragma_loop_disable_vectorization;

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
        value->long_string.external = 0xff;
        value->long_string.size = size;

        string = njs_mp_alloc(vm->mem_pool, sizeof(njs_string_t));
        if (njs_slow_path(string == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        value->long_string.data = string;

        string->start = (u_char *) start;
        string->length = 0;
        string->retain = 1;
    }

    return NJS_OK;
}


njs_int_t
njs_string_create(njs_vm_t *vm, njs_value_t *value, const char *src,
    size_t size)
{
    njs_str_t  str;

    str.start = (u_char *) src;
    str.length = size;

    return njs_string_decode_utf8(vm, value, &str);
}


njs_int_t
njs_string_new(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size, uint32_t length)
{
    u_char  *p;

    p = njs_string_alloc(vm, value, size, length);

    if (njs_fast_path(p != NULL)) {
        memcpy(p, start, size);
        return NJS_OK;
    }

    return NJS_ERROR;
}


u_char *
njs_string_alloc(njs_vm_t *vm, njs_value_t *value, uint64_t size,
    uint64_t length)
{
    uint32_t      total, map_offset, *map;
    njs_string_t  *string;

    if (njs_slow_path(size > NJS_STRING_MAX_LENGTH)) {
        njs_range_error(vm, "invalid string length");
        return NULL;
    }

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
    value->long_string.external = 0;
    value->long_string.size = size;

    if (size != length && length > NJS_STRING_MAP_STRIDE) {
        map_offset = njs_string_map_offset(size);
        total = map_offset + njs_string_map_size(length);

    } else {
        map_offset = 0;
        total = size;
    }

    string = njs_mp_alloc(vm->mem_pool, sizeof(njs_string_t) + total);

    if (njs_fast_path(string != NULL)) {
        value->long_string.data = string;

        string->start = (u_char *) string + sizeof(njs_string_t);
        string->length = length;
        string->retain = 1;

        if (map_offset != 0) {
            map = (uint32_t *) (string->start + map_offset);
            map[0] = 0;
        }

        return string->start;
    }

    njs_memory_error(vm);

    return NULL;
}


void
njs_string_truncate(njs_value_t *value, uint32_t size, uint32_t length)
{
    u_char    *dst, *src;
    uint32_t  n;

    if (size <= NJS_STRING_SHORT) {
        if (value->short_string.size == NJS_STRING_LONG) {
            dst = value->short_string.start;
            src = value->long_string.data->start;

            n = size;

            while (n != 0) {
                /* The maximum size is just 14 bytes. */
                njs_pragma_loop_disable_vectorization;

                *dst++ = *src++;
                n--;
            }
        }

        value->short_string.size = size;
        value->short_string.length = length;

    } else {
        value->long_string.size = size;
        value->long_string.data->length = length;
    }
}


njs_int_t
njs_string_hex(njs_vm_t *vm, njs_value_t *value, const njs_str_t *src)
{
    size_t     length;
    njs_str_t  dst;

    length = njs_encode_hex_length(src, &dst.length);

    dst.start = njs_string_alloc(vm, value, dst.length, length);
    if (njs_fast_path(dst.start != NULL)) {
        njs_encode_hex(&dst, src);
        return NJS_OK;
    }

    return NJS_ERROR;
}


void
njs_encode_hex(njs_str_t *dst, const njs_str_t *src)
{
    u_char        *p, c;
    size_t        i, len;
    const u_char  *start;

    static const u_char  hex[16] = "0123456789abcdef";

    len = src->length;
    start = src->start;

    p = dst->start;

    for (i = 0; i < len; i++) {
        c = start[i];
        *p++ = hex[c >> 4];
        *p++ = hex[c & 0x0f];
    }
}


size_t
njs_encode_hex_length(const njs_str_t *src, size_t *out_size)
{
    size_t  size;

    size = src->length * 2;

    if (out_size != NULL) {
        *out_size = size;
    }

    return size;
}


void
njs_encode_base64(njs_str_t *dst, const njs_str_t *src)
{
    njs_encode_base64_core(dst, src, njs_basis64_enc, 1);
}


size_t
njs_encode_base64_length(const njs_str_t *src, size_t *out_size)
{
    size_t  size;

    size = (src->length == 0) ? 0 : njs_base64_encoded_length(src->length);

    if (out_size != NULL) {
        *out_size = size;
    }

    return size;
}


static void
njs_encode_base64url(njs_str_t *dst, const njs_str_t *src)
{
    njs_encode_base64_core(dst, src, njs_basis64url_enc, 0);
}


static void
njs_encode_base64_core(njs_str_t *dst, const njs_str_t *src,
    const u_char *basis, njs_bool_t padding)
{
   u_char  *d, *s, c0, c1, c2;
   size_t  len;

    len = src->length;
    s = src->start;
    d = dst->start;

    while (len > 2) {
        c0 = s[0];
        c1 = s[1];
        c2 = s[2];

        *d++ = basis[c0 >> 2];
        *d++ = basis[((c0 & 0x03) << 4) | (c1 >> 4)];
        *d++ = basis[((c1 & 0x0f) << 2) | (c2 >> 6)];
        *d++ = basis[c2 & 0x3f];

        s += 3;
        len -= 3;
    }

    if (len > 0) {
        c0 = s[0];
        *d++ = basis[c0 >> 2];

        if (len == 1) {
            *d++ = basis[(c0 & 0x03) << 4];
            if (padding) {
                *d++ = '=';
                *d++ = '=';
            }

        } else {
            c1 = s[1];

            *d++ = basis[((c0 & 0x03) << 4) | (c1 >> 4)];
            *d++ = basis[(c1 & 0x0f) << 2];

            if (padding) {
                *d++ = '=';
            }
        }

    }

    dst->length = d - dst->start;
}


njs_int_t
njs_string_base64(njs_vm_t *vm, njs_value_t *value, const njs_str_t *src)
{
    size_t     length;
    njs_str_t  dst;

    length = njs_encode_base64_length(src, &dst.length);

    if (njs_slow_path(dst.length == 0)) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    dst.start = njs_string_alloc(vm, value, dst.length, length);
    if (njs_slow_path(dst.start == NULL)) {
        return NJS_ERROR;
    }

    njs_encode_base64(&dst, src);

    return NJS_OK;
}


njs_int_t
njs_string_base64url(njs_vm_t *vm, njs_value_t *value, const njs_str_t *src)
{
    size_t     padding;
    njs_str_t  dst;

    if (njs_slow_path(src->length == 0)) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    padding = src->length % 3;

    /*
     * Calculating the padding length: 0 -> 0, 1 -> 2, 2 -> 1.
     */
    padding = (4 >> padding) & 0x03;

    dst.length = njs_base64_encoded_length(src->length) - padding;

    dst.start = njs_string_alloc(vm, value, dst.length, dst.length);
    if (njs_slow_path(dst.start == NULL)) {
        return NJS_ERROR;
    }

    njs_encode_base64url(&dst, src);

    return NJS_OK;
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

njs_int_t
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
            length = njs_utf8_length(value->short_string.start, size);

            if (njs_slow_path(length < 0)) {
                /* Invalid UTF-8 string. */
                return length;
            }

            value->short_string.length = length;
        }

    } else {
        string->start = value->long_string.data->start;
        size = value->long_string.size;
        length = value->long_string.data->length;

        if (length == 0 && length != size) {
            length = njs_utf8_length(string->start, size);

            if (length != size) {
                if (njs_slow_path(length < 0)) {
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

                    start = njs_mp_alloc(vm->mem_pool, new_size);
                    if (njs_slow_path(start == NULL)) {
                        njs_memory_error(vm);
                        return NJS_ERROR;
                    }

                    memcpy(start, string->start, size);
                    string->start = start;
                    value->long_string.data->start = start;

                    map = (uint32_t *) (start + map_offset);
                    map[0] = 0;
                }
            }

            value->long_string.data->length = length;
        }
    }

    string->size = size;
    string->length = length;

    return length;
}


size_t
njs_string_prop(njs_string_prop_t *string, const njs_value_t *value)
{
    size_t     size;
    uintptr_t  length;

    size = value->short_string.size;

    if (size != NJS_STRING_LONG) {
        string->start = (u_char *) value->short_string.start;
        length = value->short_string.length;

    } else {
        string->start = (u_char *) value->long_string.data->start;
        size = value->long_string.size;
        length = value->long_string.data->length;
    }

    string->size = size;
    string->length = length;

    return (length == 0) ? size : length;
}


static njs_int_t
njs_string_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t           ret;
    njs_value_t         *value;
    njs_object_value_t  *object;

    if (nargs == 1) {
        value = njs_value_arg(&njs_string_empty);

    } else {
        value = &args[1];

        if (njs_slow_path(!njs_is_string(value))) {
            if (!vm->top_frame->ctor && njs_is_symbol(value)) {
                return njs_symbol_descriptive_string(vm, &vm->retval, value);
            }

            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    if (vm->top_frame->ctor) {
        object = njs_object_value_alloc(vm, NJS_OBJ_TYPE_STRING, 0, value);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        njs_set_object_value(&vm->retval, object);

    } else {
        vm->retval = *value;
    }

    return NJS_OK;
}


static const njs_object_prop_t  njs_string_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("String"),
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
        .name = njs_string("bytesFrom"),
        .value = njs_native_function(njs_string_bytes_from, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("fromCharCode"),
        .value = njs_native_function2(njs_string_from_char_code, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("fromCodePoint"),
        .value = njs_native_function2(njs_string_from_char_code, 1, 1),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_string_constructor_init = {
    njs_string_constructor_properties,
    njs_nitems(njs_string_constructor_properties),
};


static njs_int_t
njs_string_instance_length(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    size_t              size;
    uintptr_t           length;
    njs_object_t        *proto;
    njs_object_value_t  *ov;

    /*
     * This getter can be called for string primitive, String object,
     * String.prototype.  The zero should be returned for the latter case.
     */
    length = 0;

    if (njs_slow_path(njs_is_object(value))) {
        proto = njs_object(value);

        do {
            if (njs_fast_path(proto->type == NJS_OBJECT_VALUE)) {
                break;
            }

            proto = proto->__proto__;
        } while (proto != NULL);

        if (proto != NULL) {
            ov = (njs_object_value_t *) proto;
            value = &ov->value;
        }
    }

    if (njs_is_string(value)) {
        size = value->short_string.size;
        length = value->short_string.length;

        if (size == NJS_STRING_LONG) {
            size = value->long_string.size;
            length = value->long_string.data->length;
        }

        length = (length == 0) ? size : length;
    }

    njs_set_number(retval, length);

    njs_release(vm, value);

    return NJS_OK;
}


njs_bool_t
njs_string_eq(const njs_value_t *v1, const njs_value_t *v2)
{
    size_t        size, length1, length2;
    const u_char  *start1, *start2;

    size = v1->short_string.size;

    if (size != v2->short_string.size) {
        return 0;
    }

    if (size != NJS_STRING_LONG) {
        length1 = v1->short_string.length;
        length2 = v2->short_string.length;

        /*
         * Using full memcmp() comparison if at least one string
         * is a Byte string.
         */
        if (length1 != 0 && length2 != 0 && length1 != length2) {
            return 0;
        }

        start1 = v1->short_string.start;
        start2 = v2->short_string.start;

    } else {
        size = v1->long_string.size;

        if (size != v2->long_string.size) {
            return 0;
        }

        length1 = v1->long_string.data->length;
        length2 = v2->long_string.data->length;

        /*
         * Using full memcmp() comparison if at least one string
         * is a Byte string.
         */
        if (length1 != 0 && length2 != 0 && length1 != length2) {
            return 0;
        }

        start1 = v1->long_string.data->start;
        start2 = v2->long_string.data->start;
    }

    return (memcmp(start1, start2, size) == 0);
}


njs_int_t
njs_string_cmp(const njs_value_t *v1, const njs_value_t *v2)
{
    size_t        size, size1, size2;
    njs_int_t     ret;
    const u_char  *start1, *start2;

    size1 = v1->short_string.size;

    if (size1 != NJS_STRING_LONG) {
        start1 = v1->short_string.start;

    } else {
        size1 = v1->long_string.size;
        start1 = v1->long_string.data->start;
    }

    size2 = v2->short_string.size;

    if (size2 != NJS_STRING_LONG) {
        start2 = v2->short_string.start;

    } else {
        size2 = v2->long_string.size;
        start2 = v2->long_string.data->start;
    }

    size = njs_min(size1, size2);

    ret = memcmp(start1, start2, size);

    if (ret != 0) {
        return ret;
    }

    return (size1 - size2);
}


static njs_int_t
njs_string_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_STRING) {

        if (njs_is_object_string(value)) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    vm->retval = *value;

    return NJS_OK;
}


/*
 * String.prototype.toString([encoding]).
 * Returns the string as is if no additional argument is provided,
 * otherwise converts a string into an encoded string: hex, base64,
 * base64url.
 */

static njs_int_t
njs_string_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_int_t          ret;
    njs_str_t          enc, str;
    njs_value_t        value;
    njs_string_prop_t  string;

    ret = njs_string_prototype_value_of(vm, args, nargs, unused);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (nargs < 2) {
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_string(&args[1]))) {
        njs_type_error(vm, "encoding must be a string");
        return NJS_ERROR;
    }

    value = vm->retval;

    (void) njs_string_prop(&string, &value);

    njs_string_get(&args[1], &enc);

    str.length = string.size;
    str.start = string.start;

    if (enc.length == 3 && memcmp(enc.start, "hex", 3) == 0) {
        return njs_string_hex(vm, &vm->retval, &str);

    } else if (enc.length == 6 && memcmp(enc.start, "base64", 6) == 0) {
        return njs_string_base64(vm, &vm->retval, &str);

    } else if (enc.length == 9 && memcmp(enc.start, "base64url", 9) == 0) {
        return njs_string_base64url(vm, &vm->retval, &str);
    }

    njs_type_error(vm, "Unknown encoding: \"%V\"", &enc);

    return NJS_ERROR;
}


njs_int_t
njs_string_prototype_concat(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char             *p, *start;
    uint64_t           size, length, mask;
    njs_int_t          ret;
    njs_uint_t         i;
    njs_string_prop_t  string;

    if (njs_is_null_or_undefined(&args[0])) {
        njs_type_error(vm, "\"this\" argument is null or undefined");
        return NJS_ERROR;
    }

    for (i = 0; i < nargs; i++) {
        if (!njs_is_string(&args[i])) {
            ret = njs_value_to_string(vm, &args[i], &args[i]);
            if (ret != NJS_OK) {
                return ret;
            }
        }
    }

    if (nargs == 1) {
        njs_string_copy(&vm->retval, &args[0]);
        return NJS_OK;
    }

    size = 0;
    length = 0;
    mask = -1;

    for (i = 0; i < nargs; i++) {
        (void) njs_string_prop(&string, &args[i]);

        size += string.size;
        length += string.length;

        if (njs_is_byte_string(&string)) {
            mask = 0;
        }
    }

    length &= mask;

    start = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(start == NULL)) {
        return NJS_ERROR;
    }

    p = start;

    for (i = 0; i < nargs; i++) {
        (void) njs_string_prop(&string, &args[i]);

        p = memcpy(p, string.start, string.size);
        p += string.size;
    }

    return NJS_OK;
}


njs_inline njs_int_t
njs_string_object_validate(njs_vm_t *vm, njs_value_t *object)
{
    njs_int_t  ret;

    if (njs_slow_path(njs_is_null_or_undefined(object))) {
        njs_type_error(vm, "cannot convert undefined to object");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(object))) {
        ret = njs_value_to_string(vm, object, object);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    return NJS_OK;
}


/*
 * String.fromUTF8(start[, end]).
 * The method converts an UTF-8 encoded byte string to an Unicode string.
 */

static njs_int_t
njs_string_prototype_from_utf8(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    ssize_t            length;
    njs_int_t          ret;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_deprecated(vm, "String.prototype.fromUTF8()");

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_string_slice_prop(vm, &string, &slice, args, nargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (string.length != 0) {
        /* ASCII or UTF8 string. */
        return njs_string_slice(vm, &vm->retval, &string, &slice);
    }

    string.start += slice.start;

    length = njs_utf8_length(string.start, slice.length);

    if (length >= 0) {
        return njs_string_new(vm, &vm->retval, string.start, slice.length,
                              length);
    }

    vm->retval = njs_value_null;

    return NJS_OK;
}


/*
 * String.toUTF8(start[, end]).
 * The method serializes Unicode string to an UTF-8 encoded byte string.
 */

static njs_int_t
njs_string_prototype_to_utf8(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t          ret;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_deprecated(vm, "String.prototype.toUTF8()");

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    (void) njs_string_prop(&string, njs_argument(args, 0));

    string.length = 0;
    slice.string_length = string.size;

    ret = njs_string_slice_args(vm, &slice, args, nargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


/*
 * String.fromBytes(start[, end]).
 * The method converts a byte string to an Unicode string.
 */

static njs_int_t
njs_string_prototype_from_bytes(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    u_char             *p, *s, *start, *end;
    size_t             size;
    njs_int_t          ret;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    njs_deprecated(vm, "String.prototype.fromBytes()");

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_string_slice_prop(vm, &string, &slice, args, nargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

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

    if (njs_fast_path(start != NULL)) {

        if (size == slice.length) {
            memcpy(start, string.start, size);

        } else {
            s = start;
            end = string.start + slice.length;

            for (p = string.start; p < end; p++) {
                s = njs_utf8_encode(s, *p);
            }
        }

        return NJS_OK;
    }

    return NJS_ERROR;
}


/*
 * String.toBytes(start[, end]).
 * The method serializes an Unicode string to a byte string.
 * The method returns null if a character larger than 255 is
 * encountered in the Unicode string.
 */

static njs_int_t
njs_string_prototype_to_bytes(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char                *p;
    size_t                length;
    uint32_t              byte;
    njs_int_t             ret;
    const u_char          *s, *end;
    njs_slice_prop_t      slice;
    njs_string_prop_t     string;
    njs_unicode_decode_t  ctx;

    njs_deprecated(vm, "String.prototype.toBytes()");

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_string_slice_prop(vm, &string, &slice, args, nargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (string.length == 0) {
        /* Byte string. */
        return njs_string_slice(vm, &vm->retval, &string, &slice);
    }

    p = njs_string_alloc(vm, &vm->retval, slice.length, 0);

    if (njs_fast_path(p != NULL)) {

        if (string.length != string.size) {
            /* UTF-8 string. */
            end = string.start + string.size;

            s = njs_string_offset(string.start, end, slice.start);

            length = slice.length;

            njs_utf8_decode_init(&ctx);

            while (length != 0 && s < end) {
                byte = njs_utf8_decode(&ctx, &s, end);

                if (njs_slow_path(byte > 0xFF)) {
                    njs_release(vm, &vm->retval);
                    vm->retval = njs_value_null;

                    return NJS_OK;
                }

                *p++ = (u_char) byte;
                length--;
            }

        } else {
            /* ASCII string. */
            memcpy(p, string.start + slice.start, slice.length);
        }

        return NJS_OK;
    }

    return NJS_ERROR;
}


static njs_int_t
njs_string_prototype_slice(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t          ret;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_string_slice_prop(vm, &string, &slice, args, nargs);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


static njs_int_t
njs_string_prototype_substring(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    int64_t            start, end, length;
    njs_int_t          ret;
    njs_value_t        *value;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = njs_string_prop(&string, njs_argument(args, 0));

    slice.string_length = length;
    start = 0;

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
            start = 0;

        } else if (start > length) {
            start = length;
        }

        end = length;

        if (nargs > 2) {
            value = njs_arg(args, nargs, 2);

            if (njs_slow_path(!njs_is_number(value))) {
                ret = njs_value_to_integer(vm, value, &end);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }

            } else {
                end = njs_number_to_integer(njs_number(value));
            }

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


static njs_int_t
njs_string_prototype_substr(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t            start, length, n;
    njs_int_t          ret;
    njs_value_t        *value;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = njs_string_prop(&string, njs_argument(args, 0));

    slice.string_length = length;
    start = 0;

    if (nargs > 1) {
        value = njs_arg(args, nargs, 1);

        if (njs_slow_path(!njs_is_number(value))) {
            ret = njs_value_to_integer(vm, value, &start);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

        } else {
            start = njs_number_to_integer(njs_number(value));
        }

        if (start < length) {
            if (start < 0) {
                start += length;

                if (start < 0) {
                    start = 0;
                }
            }

            length -= start;

            if (nargs > 2) {
                value = njs_arg(args, nargs, 2);

                if (njs_slow_path(!njs_is_number(value))) {
                    ret = njs_value_to_integer(vm, value, &n);
                    if (njs_slow_path(ret != NJS_OK)) {
                        return ret;
                    }

                } else {
                    n = njs_number_to_integer(njs_number(value));
                }

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


static njs_int_t
njs_string_prototype_char_at(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t             length;
    int64_t            start;
    njs_int_t          ret;
    njs_slice_prop_t   slice;
    njs_string_prop_t  string;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    slice.string_length = njs_string_prop(&string, njs_argument(args, 0));

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &start);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = 1;

    if (start < 0 || start >= (int64_t) slice.string_length) {
        start = 0;
        length = 0;
    }

    slice.start = start;
    slice.length = length;

    return njs_string_slice(vm, &vm->retval, &string, &slice);
}


static njs_int_t
njs_string_slice_prop(njs_vm_t *vm, njs_string_prop_t *string,
    njs_slice_prop_t *slice, njs_value_t *args, njs_uint_t nargs)
{
    slice->string_length = njs_string_prop(string, &args[0]);

    return njs_string_slice_args(vm, slice, args, nargs);
}


static njs_int_t
njs_string_slice_args(njs_vm_t *vm, njs_slice_prop_t *slice, njs_value_t *args,
    njs_uint_t nargs)
{
    int64_t      start, end, length;
    njs_int_t    ret;
    njs_value_t  *value;

    length = slice->string_length;

    value = njs_arg(args, nargs, 1);

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
    }

    if (start >= length) {
        start = 0;
        length = 0;

    } else {
        value = njs_arg(args, nargs, 2);

        if (njs_slow_path(!njs_is_number(value))) {
            if (njs_is_defined(value)) {
                ret = njs_value_to_integer(vm, value, &end);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }

            } else {
                end = length;
            }

        } else {
            end = njs_number_to_integer(njs_number(value));
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

    slice->start = start;
    slice->length = length;

    return NJS_OK;
}


void
njs_string_slice_string_prop(njs_string_prop_t *dst,
    const njs_string_prop_t *string, const njs_slice_prop_t *slice)
{
    size_t        size, n, length;
    const u_char  *p, *start, *end;

    length = slice->length;
    start = string->start;

    if (string->size == slice->string_length) {
        /* Byte or ASCII string. */
        start += slice->start;
        size = slice->length;

        if (string->length == 0) {
            /* Byte string. */
            length = 0;
        }

    } else {
        /* UTF-8 string. */
        end = start + string->size;

        if (slice->start < slice->string_length) {
            start = njs_string_offset(start, end, slice->start);

            /* Evaluate size of the slice in bytes and adjust length. */
            p = start;
            n = length;

            while (n != 0 && p < end) {
                p = njs_utf8_next(p, end);
                n--;
            }

            size = p - start;
            length -= n;

        } else {
            length = 0;
            size = 0;
        }
    }

    dst->start = (u_char *) start;
    dst->length = length;
    dst->size = size;
}


njs_int_t
njs_string_slice(njs_vm_t *vm, njs_value_t *dst,
    const njs_string_prop_t *string, const njs_slice_prop_t *slice)
{
    njs_string_prop_t  prop;

    njs_string_slice_string_prop(&prop, string, slice);

    if (njs_fast_path(prop.size != 0)) {
        return njs_string_new(vm, dst, prop.start, prop.size, prop.length);
    }

    *dst = njs_string_empty;

    return NJS_OK;
}


static njs_int_t
njs_string_prototype_char_code_at(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double                num;
    size_t                length;
    int64_t               index;
    uint32_t              code;
    njs_int_t             ret;
    const u_char          *start, *end;
    njs_string_prop_t     string;
    njs_unicode_decode_t  ctx;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = njs_string_prop(&string, njs_argument(args, 0));

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &index);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(index < 0 || index >= (int64_t) length)) {
        num = NAN;
        goto done;
    }

    if (length == string.size) {
        /* Byte or ASCII string. */
        code = string.start[index];

    } else {
        njs_utf8_decode_init(&ctx);

        /* UTF-8 string. */
        end = string.start + string.size;
        start = njs_string_offset(string.start, end, index);
        code = njs_utf8_decode(&ctx, &start, end);
    }

    num = code;

done:

    njs_set_number(&vm->retval, num);

    return NJS_OK;
}


/*
 * String.bytesFrom(array-like).
 * Converts an array-like object containing octets into a byte string.
 *
 * String.bytesFrom(string[, encoding]).
 * Converts a string using provided encoding: hex, base64, base64url to
 * a byte string.
 */

static njs_int_t
njs_string_bytes_from(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t  *value;

    njs_deprecated(vm, "String.bytesFrom()");

    value = njs_arg(args, nargs, 1);

    if (njs_is_string(value)) {
        return njs_string_bytes_from_string(vm, value, njs_arg(args, nargs, 2));

    } else if (njs_is_object(value)) {

        if (njs_is_object_string(value)) {
            value = njs_object_value(value);
            return njs_string_bytes_from_string(vm, value,
                                                njs_arg(args, nargs, 2));
        }

        return njs_string_bytes_from_array_like(vm, value);
    }

    njs_type_error(vm, "value must be a string or array-like object");

    return NJS_ERROR;
}


static njs_int_t
njs_string_bytes_from_array_like(njs_vm_t *vm, njs_value_t *value)
{
    u_char              *p;
    int64_t             length;
    uint32_t            u32;
    njs_int_t           ret;
    njs_array_t         *array;
    njs_value_t         *octet, index, prop;
    njs_array_buffer_t  *buffer;

    array = NULL;
    buffer = NULL;

    switch (value->type) {
    case NJS_ARRAY:
        array = njs_array(value);
        length = array->length;
        break;

    case NJS_ARRAY_BUFFER:
    case NJS_TYPED_ARRAY:

        if (njs_is_typed_array(value)) {
            buffer = njs_typed_array(value)->buffer;

        } else {
            buffer = njs_array_buffer(value);
        }

        length = buffer->size;
        break;

    default:
        ret = njs_object_length(vm, value, &length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }
    }

    p = njs_string_alloc(vm, &vm->retval, length, 0);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    if (array != NULL) {
        octet = array->start;

        while (length != 0) {
            ret = njs_value_to_uint32(vm, octet, &u32);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            *p++ = (u_char) u32;
            octet++;
            length--;
        }

    } else if (buffer != NULL) {
        memcpy(p, buffer->u.u8, length);

    } else {
        p += length - 1;

        while (length != 0) {
            njs_set_number(&index, length - 1);

            ret = njs_value_property(vm, value, &index, &prop);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            ret = njs_value_to_uint32(vm, &prop, &u32);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            *p-- = (u_char) u32;
            length--;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_string_bytes_from_string(njs_vm_t *vm, const njs_value_t *string,
    const njs_value_t *encoding)
{
    njs_str_t  enc, str;

    if (!njs_is_string(encoding)) {
        njs_type_error(vm, "\"encoding\" must be a string");
        return NJS_ERROR;
    }

    njs_string_get(encoding, &enc);
    njs_string_get(string, &str);

    if (enc.length == 3 && memcmp(enc.start, "hex", 3) == 0) {
        return njs_string_decode_hex(vm, &vm->retval, &str);

    } else if (enc.length == 6 && memcmp(enc.start, "base64", 6) == 0) {
        return njs_string_decode_base64(vm, &vm->retval, &str);

    } else if (enc.length == 9 && memcmp(enc.start, "base64url", 9) == 0) {
        return njs_string_decode_base64url(vm, &vm->retval, &str);
    }

    njs_type_error(vm, "Unknown encoding: \"%V\"", &enc);

    return NJS_ERROR;
}


size_t
njs_decode_hex_length(const njs_str_t *src, size_t *out_size)
{
    if (out_size != NULL) {
        *out_size = src->length / 2;
    }

    return 0;
}


void
njs_decode_hex(njs_str_t *dst, const njs_str_t *src)
{
    u_char        *p;
    size_t        len;
    njs_int_t     c;
    njs_uint_t    i, n;
    const u_char  *start;

    n = 0;
    p = dst->start;

    start = src->start;
    len = src->length;

    for (i = 0; i < len; i++) {
        c = njs_char_to_hex(start[i]);
        if (njs_slow_path(c < 0)) {
            break;
        }

        n = n * 16 + c;

        if ((i & 1) != 0) {
            *p++ = (u_char) n;
            n = 0;
        }
    }

    dst->length -= (dst->start + dst->length) - p;
}


void
njs_decode_utf8(njs_str_t *dst, const njs_str_t *src)
{
    njs_unicode_decode_t  ctx;

    njs_utf8_decode_init(&ctx);

    (void) njs_utf8_stream_encode(&ctx, src->start, src->start + src->length,
                                  dst->start, 1, 0);
}


size_t
njs_decode_utf8_length(const njs_str_t *src, size_t *out_size)
{
    njs_unicode_decode_t  ctx;

    njs_utf8_decode_init(&ctx);

    return njs_utf8_stream_length(&ctx, src->start, src->length, 1, 0,
                                  out_size);
}


njs_int_t
njs_string_decode_utf8(njs_vm_t *vm, njs_value_t *value, const njs_str_t *src)
{
    size_t     length;
    njs_str_t  dst;

    length = njs_decode_utf8_length(src, &dst.length);
    dst.start = njs_string_alloc(vm, value, dst.length, length);

    if (njs_fast_path(dst.start != NULL)) {
        njs_decode_utf8(&dst, src);
        return NJS_OK;
    }

    return NJS_ERROR;
}


njs_int_t
njs_string_decode_hex(njs_vm_t *vm, njs_value_t *value, const njs_str_t *src)
{
    size_t     size, length;
    njs_str_t  dst;

    length = njs_decode_hex_length(src, &size);

    if (njs_slow_path(size == 0)) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    dst.start = njs_string_alloc(vm, value, size, length);
    if (njs_slow_path(dst.start == NULL)) {
        return NJS_ERROR;
    }

    dst.length = size;

    njs_decode_hex(&dst, src);

    if (njs_slow_path(dst.length != size)) {
        njs_string_truncate(value, dst.length, 0);
    }

    return NJS_OK;
}


static size_t
njs_decode_base64_length_core(const njs_str_t *src, const u_char *basis,
    size_t *out_size)
{
    uint    pad;
    size_t  len;

    for (len = 0; len < src->length; len++) {
        if (basis[src->start[len]] == 77) {
            break;
        }
    }

    pad = 0;

    if (len % 4 != 0) {
        pad = 4 - (len % 4);
        len += pad;
    }

    len = njs_base64_decoded_length(len, pad);

    if (out_size != NULL) {
        *out_size = len;
    }

    return 0;
}


size_t
njs_decode_base64_length(const njs_str_t *src, size_t *out_size)
{
    return njs_decode_base64_length_core(src, njs_basis64, out_size);
}


size_t
njs_decode_base64url_length(const njs_str_t *src, size_t *out_size)
{
    return njs_decode_base64_length_core(src, njs_basis64url, out_size);
}


static void
njs_decode_base64_core(njs_str_t *dst, const njs_str_t *src,
    const u_char *basis)
{
    size_t  len;
    u_char  *d, *s;

    s = src->start;
    d = dst->start;

    len = dst->length;

    while (len >= 3) {
        *d++ = (u_char) (basis[s[0]] << 2 | basis[s[1]] >> 4);
        *d++ = (u_char) (basis[s[1]] << 4 | basis[s[2]] >> 2);
        *d++ = (u_char) (basis[s[2]] << 6 | basis[s[3]]);

        s += 4;
        len -= 3;
    }

    if (len >= 1) {
        *d++ = (u_char) (basis[s[0]] << 2 | basis[s[1]] >> 4);
    }

    if (len >= 2) {
        *d++ = (u_char) (basis[s[1]] << 4 | basis[s[2]] >> 2);
    }
}


void
njs_decode_base64(njs_str_t *dst, const njs_str_t *src)
{
    njs_decode_base64_core(dst, src, njs_basis64);
}


void
njs_decode_base64url(njs_str_t *dst, const njs_str_t *src)
{
    njs_decode_base64_core(dst, src, njs_basis64url);
}


static njs_int_t
njs_string_decode_base64_core(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src, njs_bool_t url)
{
    size_t     length;
    const u_char *basis;
    njs_str_t  dst;

    basis = (url) ? njs_basis64url : njs_basis64;

    length = njs_decode_base64_length_core(src, basis, &dst.length);

    if (njs_slow_path(dst.length == 0)) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    dst.start = njs_string_alloc(vm, value, dst.length, length);
    if (njs_slow_path(dst.start == NULL)) {
        return NJS_ERROR;
    }

    njs_decode_base64_core(&dst, src, basis);

    return NJS_OK;
}


njs_int_t
njs_string_decode_base64(njs_vm_t *vm, njs_value_t *value, const njs_str_t *src)
{
    return njs_string_decode_base64_core(vm, value, src, 0);
}


njs_int_t
njs_string_decode_base64url(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src)
{
    return njs_string_decode_base64_core(vm, value, src, 1);
}


static njs_int_t
njs_string_from_char_code(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t is_point)
{
    double                num;
    u_char                *p, *start, *end;
    ssize_t               len;
    int32_t               code;
    uint32_t              cp;
    uint64_t              length, size;
    njs_int_t             ret;
    njs_uint_t            i;
    njs_unicode_decode_t  ctx;
    u_char                buf[4];

    size = 0;
    length = 0;

    cp = 0x00;
    end = buf + sizeof(buf);

    njs_utf16_decode_init(&ctx);

    for (i = 1; i < nargs; i++) {
        if (!njs_is_numeric(&args[i])) {
            ret = njs_value_to_numeric(vm, &args[i], &args[i]);
            if (ret != NJS_OK) {
                return ret;
            }
        }

        if (is_point) {
            num = njs_number(&args[i]);
            if (isnan(num)) {
                goto range_error;
            }

            code = num;

            if (code != num || code < 0 || code > 0x10FFFF) {
                goto range_error;
            }

        } else {
            code = njs_number_to_uint16(njs_number(&args[i]));
        }

        start = buf;
        len = njs_utf16_encode(code, &start, end);

        start = buf;
        cp = njs_utf16_decode(&ctx, (const u_char **) &start, start + len);

        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            if (cp == NJS_UNICODE_CONTINUE) {
                continue;
            }

            cp = NJS_UNICODE_REPLACEMENT;
        }

        size += njs_utf8_size(cp);
        length++;
    }

    if (cp == NJS_UNICODE_CONTINUE) {
        size += njs_utf8_size(NJS_UNICODE_REPLACEMENT);
        length++;
    }

    p = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    njs_utf16_decode_init(&ctx);

    for (i = 1; i < nargs; i++) {
        if (is_point) {
            code = njs_number(&args[i]);

        } else {
            code = njs_number_to_uint16(njs_number(&args[i]));
        }

        start = buf;
        len = njs_utf16_encode(code, &start, end);

        start = buf;
        cp = njs_utf16_decode(&ctx, (const u_char **) &start, start + len);

        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            if (cp == NJS_UNICODE_CONTINUE && i + 1 != nargs) {
                continue;
            }

            cp = NJS_UNICODE_REPLACEMENT;
        }

        p = njs_utf8_encode(p, cp);
    }

    return NJS_OK;

range_error:

    njs_range_error(vm, NULL);

    return NJS_ERROR;
}


static int64_t
njs_string_index_of(njs_string_prop_t *string, njs_string_prop_t *search,
    size_t from)
{
    size_t        index, length, search_length;
    const u_char  *p, *end;

    length = (string->length == 0) ? string->size : string->length;

    if (njs_slow_path(search->size == 0)) {
        return (from < length) ? from : length;
    }

    index = from;
    search_length = (search->length == 0) ? search->size : search->length;

    if (length - index >= search_length) {
        end = string->start + string->size;

        if (string->size == length) {
            /* Byte or ASCII string. */

            end -= (search->size - 1);

            for (p = string->start + index; p < end; p++) {
                if (memcmp(p, search->start, search->size) == 0) {
                    return index;
                }

                index++;
            }

        } else {
            /* UTF-8 string. */

            p = njs_string_offset(string->start, end, index);
            end -= search->size - 1;

            while (p < end) {
                if (memcmp(p, search->start, search->size) == 0) {
                    return index;
                }

                index++;
                p = njs_utf8_next(p, end);
            }
        }
    }

    return -1;
}


static njs_int_t
njs_string_prototype_index_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t            from, length;
    njs_int_t          ret;
    njs_value_t        *this, *search, *pos, search_lvalue, pos_lvalue;
    njs_string_prop_t  string, s;

    this = njs_argument(args, 0);

    if (njs_slow_path(njs_is_null_or_undefined(this))) {
        njs_type_error(vm, "cannot convert \"%s\"to object",
                       njs_type_string(this->type));
        return NJS_ERROR;
    }

    ret = njs_value_to_string(vm, this, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    search = njs_lvalue_arg(&search_lvalue, args, nargs, 1);
    ret = njs_value_to_string(vm, search, search);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    pos = njs_lvalue_arg(&pos_lvalue, args, nargs, 2);
    ret = njs_value_to_integer(vm, pos, &from);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = njs_string_prop(&string, this);
    (void) njs_string_prop(&s, search);

    from = njs_min(njs_max(from, 0), length);

    njs_set_number(&vm->retval, njs_string_index_of(&string, &s, from));

    return NJS_OK;
}


static njs_int_t
njs_string_prototype_last_index_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double             pos;
    int64_t            index, start, length, search_length;
    njs_int_t          ret;
    njs_value_t        *this, *search, search_lvalue;
    const u_char       *p, *end;
    njs_string_prop_t  string, s;

    this = njs_argument(args, 0);

    if (njs_slow_path(njs_is_null_or_undefined(this))) {
        njs_type_error(vm, "cannot convert \"%s\"to object",
                       njs_type_string(this->type));
        return NJS_ERROR;
    }

    ret = njs_value_to_string(vm, this, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    search = njs_lvalue_arg(&search_lvalue, args, nargs, 1);
    ret = njs_value_to_string(vm, search, search);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_to_number(vm, njs_arg(args, nargs, 2), &pos);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!isnan(pos)) {
        start = njs_number_to_integer(pos);

    } else {
        start = INT64_MAX;
    }

    length = njs_string_prop(&string, this);

    start = njs_min(njs_max(start, 0), length);

    search_length = njs_string_prop(&s, search);

    index = length - search_length;

    if (index > start) {
        index = start;
    }

    end = string.start + string.size;

    if (string.size == (size_t) length) {
        /* Byte or ASCII string. */

        p = &string.start[index];

        if (p > end - s.size) {
            p = end - s.size;
        }

        for (; p >= string.start; p--) {
            if (memcmp(p, s.start, s.size) == 0) {
                index = p - string.start;
                goto done;
            }
        }

        index = -1;

    } else {
        /* UTF-8 string. */

        if (index < 0 || index == length) {
            index = (search_length == 0) ? index : -1;
            goto done;
        }

        p = njs_string_offset(string.start, end, index);

        for (; p >= string.start; p = njs_utf8_prev(p)) {
            if ((p + s.size) <= end && memcmp(p, s.start, s.size) == 0) {
                goto done;
            }

            index--;
        }

        index = -1;
    }

done:

    njs_set_number(&vm->retval, index);

    return NJS_OK;
}


static njs_int_t
njs_string_prototype_includes(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int64_t            index, length, search_length;
    njs_int_t          ret;
    njs_value_t        *value;
    const u_char       *p, *end;
    const njs_value_t  *retval;
    njs_string_prop_t  string, search;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    retval = &njs_value_true;

    if (nargs > 1) {
        value = njs_argument(args, 1);

        if (njs_slow_path(!njs_is_string(value))) {
            ret = njs_value_to_string(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        search_length = njs_string_prop(&search, value);

        if (nargs > 2) {
            value = njs_argument(args, 2);

            if (njs_slow_path(!njs_is_number(value))) {
                ret = njs_value_to_integer(vm, value, &index);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }

            } else {
                index = njs_number_to_integer(njs_number(value));
            }

            if (index < 0) {
                index = 0;
            }

        } else {
            index = 0;
        }

        if (search_length == 0) {
            goto done;
        }

        length = njs_string_prop(&string, &args[0]);

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

    return NJS_OK;
}


static njs_int_t
njs_string_prototype_starts_or_ends_with(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t starts)
{
    int64_t            index, length, search_length;
    njs_int_t          ret;
    njs_value_t        *value, lvalue;
    const u_char       *p, *end;
    const njs_value_t  *retval;
    njs_string_prop_t  string, search;

    retval = &njs_value_true;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    value = njs_lvalue_arg(&lvalue, args, nargs, 1);

    if (njs_slow_path(!njs_is_string(value))) {
        ret = njs_value_to_string(vm, value, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    search_length = njs_string_prop(&search, value);

    value = njs_arg(args, nargs, 2);

    if (njs_slow_path(!njs_is_number(value))) {
        index = -1;

        if (!njs_is_undefined(value)) {
            ret = njs_value_to_integer(vm, value, &index);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

    } else {
        index = njs_number_to_integer(njs_number(value));
    }

    if (search_length == 0) {
        goto done;
    }

    if (nargs > 1) {
        length = njs_string_prop(&string, &args[0]);

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

    return NJS_OK;
}


/*
 * njs_string_offset() assumes that index is correct.
 */

const u_char *
njs_string_offset(const u_char *start, const u_char *end, size_t index)
{
    uint32_t    *map;
    njs_uint_t  skip;

    if (index >= NJS_STRING_MAP_STRIDE) {
        map = njs_string_map_start(end);

        if (map[0] == 0) {
            njs_string_offset_map_init(start, end - start);
        }

        start += map[index / NJS_STRING_MAP_STRIDE - 1];
    }

    for (skip = index % NJS_STRING_MAP_STRIDE; skip != 0; skip--) {
        start = njs_utf8_next(start, end);
    }

    return start;
}


/*
 * njs_string_index() assumes that offset is correct.
 */

uint32_t
njs_string_index(njs_string_prop_t *string, uint32_t offset)
{
    uint32_t      *map, last, index;
    const u_char  *p, *start, *end;

    if (string->size == string->length) {
        return offset;
    }

    last = 0;
    index = 0;

    if (string->length > NJS_STRING_MAP_STRIDE) {

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
        p = njs_utf8_next(p, end);
    }

    return index;
}


void
njs_string_offset_map_init(const u_char *start, size_t size)
{
    size_t        offset;
    uint32_t      *map;
    njs_uint_t    n;
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
        p = njs_utf8_next(p, end);

        offset--;

    } while (p < end);
}


/*
 * The method supports only simple folding.  For example, Turkish "İ"
 * folding "\u0130" to "\u0069\u0307" is not supported.
 */

static njs_int_t
njs_string_prototype_to_lower_case(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    size_t             size, length;
    u_char             *p;
    uint32_t           code;
    njs_int_t          ret;
    const u_char       *s, *end;
    njs_string_prop_t  string;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    (void) njs_string_prop(&string, njs_argument(args, 0));

    if (njs_is_byte_or_ascii_string(&string)) {

        p = njs_string_alloc(vm, &vm->retval, string.size, string.length);
        if (njs_slow_path(p == NULL)) {
            return NJS_ERROR;
        }

        s = string.start;
        size = string.size;

        while (size != 0) {
            *p++ = njs_lower_case(*s++);
            size--;
        }

    } else {
        /* UTF-8 string. */
        s = string.start;
        end = s + string.size;
        length = string.length;

        size = 0;

        while (length != 0) {
            code = njs_utf8_lower_case(&s, end);
            size += njs_utf8_size(code);
            length--;
        }

        p = njs_string_alloc(vm, &vm->retval, size, string.length);
        if (njs_slow_path(p == NULL)) {
            return NJS_ERROR;
        }

        s = string.start;
        length = string.length;

        while (length != 0) {
            code = njs_utf8_lower_case(&s, end);
            p = njs_utf8_encode(p, code);
            length--;
        }
    }

    return NJS_OK;
}


/*
 * The method supports only simple folding.  For example, German "ß"
 * folding "\u00DF" to "\u0053\u0053" is not supported.
 */

static njs_int_t
njs_string_prototype_to_upper_case(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    size_t             size, length;
    u_char             *p;
    uint32_t           code;
    njs_int_t          ret;
    const u_char       *s, *end;
    njs_string_prop_t  string;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    (void) njs_string_prop(&string, njs_argument(args, 0));

    if (njs_is_byte_or_ascii_string(&string)) {

        p = njs_string_alloc(vm, &vm->retval, string.size, string.length);
        if (njs_slow_path(p == NULL)) {
            return NJS_ERROR;
        }

        s = string.start;
        size = string.size;

        while (size != 0) {
            *p++ = njs_upper_case(*s++);
            size--;
        }

    } else {
        /* UTF-8 string. */
        s = string.start;
        end = s + string.size;
        length = string.length;

        size = 0;

        while (length != 0) {
            code = njs_utf8_upper_case(&s, end);
            size += njs_utf8_size(code);
            length--;
        }

        p = njs_string_alloc(vm, &vm->retval, size, string.length);
        if (njs_slow_path(p == NULL)) {
            return NJS_ERROR;
        }

        s = string.start;
        length = string.length;

        while (length != 0) {
            code = njs_utf8_upper_case(&s, end);
            p = njs_utf8_encode(p, code);
            length--;
        }
    }

    return NJS_OK;
}


uint32_t
njs_string_trim(const njs_value_t *value, njs_string_prop_t *string,
    unsigned mode)
{
    uint32_t              cp, trim;
    const u_char          *p, *prev, *start, *end;
    njs_unicode_decode_t  ctx;

    trim = 0;

    njs_string_prop(string, value);

    start = string->start;
    end = string->start + string->size;

    if (njs_is_byte_or_ascii_string(string)) {

        if (mode & NJS_TRIM_START) {
            for ( ;; ) {
                if (start == end) {
                    break;
                }

                if (njs_is_whitespace(*start)) {
                    start++;
                    trim++;
                    continue;
                }

                break;
            }
        }

        if (mode & NJS_TRIM_END) {
            for ( ;; ) {
                if (start == end) {
                    break;
                }

                end--;

                if (njs_is_whitespace(*end)) {
                    trim++;
                    continue;
                }

                end++;
                break;
            }
        }

    } else {
        /* UTF-8 string. */

        if (mode & NJS_TRIM_START) {
            njs_utf8_decode_init(&ctx);

            for ( ;; ) {
                if (start == end) {
                    break;
                }

                p = start;
                cp = njs_utf8_decode(&ctx, &start, end);

                if (njs_utf8_is_whitespace(cp)) {
                    trim++;
                    continue;
                }

                start = p;
                break;
            }
        }

        if (mode & NJS_TRIM_END) {
            prev = end;

            njs_utf8_decode_init(&ctx);

            for ( ;; ) {
                if (start == prev) {
                    end = prev;
                    break;
                }

                prev = njs_utf8_prev(prev);
                p = prev;
                cp = njs_utf8_decode(&ctx, &p, end);

                if (njs_utf8_is_whitespace(cp)) {
                    trim++;
                    continue;
                }

                end = p;
                break;
            }
        }
    }

    if (start == end) {
        string->length = 0;
        string->size = 0;
        return trim;
    }

    string->start = (u_char *) start;
    string->size = end - start;

    if (string->length != 0) {
        string->length -= trim;
    }

    return trim;
}


static njs_int_t
njs_string_prototype_trim(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t mode)
{
    uint32_t           trim;
    njs_int_t          ret;
    njs_value_t        *value;
    njs_string_prop_t  string;

    value = njs_argument(args, 0);
    ret = njs_string_object_validate(vm, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    trim = njs_string_trim(value, &string, mode);

    if (trim == 0) {
        njs_value_assign(&vm->retval, value);
        return NJS_OK;
    }

    if (string.size == 0) {
        njs_value_assign(&vm->retval, &njs_string_empty);
        return NJS_OK;
    }

    return njs_string_new(vm, &vm->retval, string.start, string.size,
                          string.length);
}


static njs_int_t
njs_string_prototype_repeat(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char             *p;
    int64_t            n, max;
    uint64_t           size, length;
    njs_int_t          ret;
    njs_value_t        *this;
    njs_string_prop_t  string;

    this = njs_argument(args, 0);

    if (njs_slow_path(njs_is_null_or_undefined(this))) {
        njs_type_error(vm, "cannot convert \"%s\"to object",
                       njs_type_string(this->type));
        return NJS_ERROR;
    }

    ret = njs_value_to_string(vm, this, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &n);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(n < 0 || n == INT64_MAX)) {
        njs_range_error(vm, NULL);
        return NJS_ERROR;
    }

    (void) njs_string_prop(&string, this);

    if (njs_slow_path(n == 0 || string.size == 0)) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    max = NJS_STRING_MAX_LENGTH / string.size;

    if (njs_slow_path(n >= max)) {
        njs_range_error(vm, NULL);
        return NJS_ERROR;
    }

    size = string.size * n;
    length = string.length * n;

    p = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    while (n != 0) {
        p = memcpy(p, string.start, string.size);
        p += string.size;
        n--;
    }

    return NJS_OK;
}


static njs_int_t
njs_string_prototype_pad(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t pad_start)
{
    u_char             *p, *start;
    size_t             padding, trunc, new_size;
    int64_t            length, new_length;
    uint32_t           n, pad_length;
    njs_int_t          ret;
    njs_value_t        *value, *pad;
    const u_char       *end;
    njs_string_prop_t  string, pad_string;

    static const njs_value_t  string_space = njs_string(" ");

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = njs_string_prop(&string, njs_argument(args, 0));

    new_length = 0;

    if (nargs > 1) {
        value = njs_argument(args, 1);

        if (njs_slow_path(!njs_is_number(value))) {
            ret = njs_value_to_integer(vm, value, &new_length);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

        } else {
            new_length = njs_number_to_integer(njs_number(value));
        }
    }

    if (new_length <= length) {
        vm->retval = args[0];
        return NJS_OK;
    }

    if (njs_slow_path(new_length >= NJS_STRING_MAX_LENGTH)) {
        njs_range_error(vm, NULL);
        return NJS_ERROR;
    }

    padding = new_length - length;

    /* GCC and Clang complain about uninitialized n and trunc. */
    n = 0;
    trunc = 0;

    pad = njs_arg(args, nargs, 2);

    if (njs_slow_path(!njs_is_string(pad))) {
        if (njs_is_undefined(pad)) {
            pad = njs_value_arg(&string_space);

        } else {
            ret = njs_value_to_string(vm, pad, pad);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }
        }
    }

    pad_length = njs_string_prop(&pad_string, pad);

    if (pad_string.size == 0) {
        vm->retval = args[0];
        return NJS_OK;
    }

    if (pad_string.size > 1) {
        n = padding / pad_length;
        trunc = padding % pad_length;

        if (pad_string.size != (size_t) pad_length) {
            /* UTF-8 string. */
            end = pad_string.start + pad_string.size;
            end = njs_string_offset(pad_string.start, end, trunc);

            trunc = end - pad_string.start;
            padding = pad_string.size * n + trunc;
        }
    }

    new_size = string.size + padding;

    start = njs_string_alloc(vm, &vm->retval, new_size, new_length);
    if (njs_slow_path(start == NULL)) {
        return NJS_ERROR;
    }

    p = start;

    if (pad_start) {
        start += padding;

    } else {
        p += string.size;
    }

    memcpy(start, string.start, string.size);

    if (pad_string.size == 1) {
        njs_memset(p, pad_string.start[0], padding);

    } else {
        while (n != 0) {
            memcpy(p, pad_string.start, pad_string.size);
            p += pad_string.size;
            n--;
        }

        memcpy(p, pad_string.start, trunc);
    }

    return NJS_OK;
}


static njs_int_t
njs_string_prototype_search(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t                c;
    njs_int_t             ret, index;
    njs_uint_t            n;
    njs_value_t           *value;
    njs_string_prop_t     string;
    njs_regexp_pattern_t  *pattern;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    index = 0;

    if (nargs > 1) {
        value = njs_argument(args, 1);

        switch (value->type) {

        case NJS_REGEXP:
            pattern = njs_regexp_pattern(value);
            break;

        case NJS_UNDEFINED:
            goto done;

        default:
            if (njs_slow_path(!njs_is_string(value))) {
                ret = njs_value_to_string(vm, value, value);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }
            }

            (void) njs_string_prop(&string, value);

            if (string.size != 0) {
                pattern = njs_regexp_pattern_create(vm, string.start,
                                                    string.size, 0);
                if (njs_slow_path(pattern == NULL)) {
                    return NJS_ERROR;
                }

                break;
            }

            goto done;
        }

        index = -1;

        (void) njs_string_prop(&string, &args[0]);

        n = (string.length != 0);

        if (njs_regex_is_valid(&pattern->regex[n])) {
            ret = njs_regexp_match(vm, &pattern->regex[n], string.start,
                                   0, string.size, vm->single_match_data);
            if (ret >= 0) {
                c = njs_regex_capture(vm->single_match_data, 0);
                index = njs_string_index(&string, c);

            } else if (ret == NJS_ERROR) {
                return NJS_ERROR;
            }
        }
    }

done:

    njs_set_number(&vm->retval, index);

    return NJS_OK;
}


static njs_int_t
njs_string_prototype_match(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t             string;
    njs_int_t             ret;
    njs_value_t           arguments[2];
    njs_regexp_pattern_t  *pattern;

    ret = njs_string_object_validate(vm, njs_argument(args, 0));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    arguments[1] = args[0];

    string.start = NULL;
    string.length = 0;

    if (nargs > 1) {

        if (njs_is_regexp(&args[1])) {
            pattern = njs_regexp_pattern(&args[1]);

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

        if (!njs_is_string(&args[1])) {
            if (!njs_is_undefined(&args[1])) {
                ret = njs_value_to_string(vm, &args[1], &args[1]);
                if (njs_slow_path(ret != NJS_OK)) {
                    return ret;
                }

                njs_string_get(&args[1], &string);
            }

        } else {
            njs_string_get(&args[1], &string);
        }

        /* A void value. */
    }

    ret = njs_regexp_create(vm, &arguments[0], string.start, string.length, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

match:

    return njs_regexp_prototype_exec(vm, arguments, nargs, unused);
}


static njs_int_t
njs_string_match_multiple(njs_vm_t *vm, njs_value_t *args,
    njs_regexp_pattern_t *pattern)
{
    size_t             c0, c1;
    int32_t            size, length;
    njs_int_t          ret;
    njs_utf8_t         utf8;
    njs_array_t        *array;
    const u_char       *p, *start, *end;
    njs_regexp_utf8_t  type;
    njs_string_prop_t  string;

    njs_set_number(&args[1].data.u.regexp->last_index, 0);
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

    if (njs_regex_is_valid(&pattern->regex[type])) {

        array = njs_array_alloc(vm, 0, 0, NJS_ARRAY_SPARE);
        if (njs_slow_path(array == NULL)) {
            return NJS_ERROR;
        }

        p = string.start;
        end = p + string.size;

        do {
            ret = njs_regexp_match(vm, &pattern->regex[type], p, 0, string.size,
                                   vm->single_match_data);
            if (ret < 0) {
                if (njs_fast_path(ret == NJS_DECLINED)) {
                    break;
                }

                njs_internal_error(vm, "njs_regexp_match() failed");

                return NJS_ERROR;
            }

            ret = njs_array_expand(vm, array, 0, 1);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            c0 = njs_regex_capture(vm->single_match_data, 0);
            c1 = njs_regex_capture(vm->single_match_data, 1);
            start = p + c0;

            if (c1 == 0) {
                if (start < end) {
                    p = (utf8 != NJS_STRING_BYTE) ? njs_utf8_next(start, end)
                                                  : start + 1;
                    string.size = end - p;

                } else {
                    /* To exit the loop. */
                    p++;
                }

                size = 0;
                length = 0;

            } else {
                p += c1;
                string.size -= c1;

                size = c1 - c0;
                length = njs_string_calc_length(utf8, start, size);
            }

            ret = njs_string_new(vm, &array->start[array->length],
                                 start, size, length);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            array->length++;

        } while (p <= end);

        njs_set_array(&vm->retval, array);
    }

    return NJS_OK;
}


static njs_int_t
njs_string_prototype_split(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t             size;
    uint32_t           limit;
    njs_int_t          ret;
    njs_utf8_t         utf8;
    njs_bool_t         undefined;
    njs_value_t        *this, *separator, *value;
    njs_value_t        separator_lvalue, limit_lvalue, splitter;
    njs_array_t        *array;
    const u_char       *p, *start, *next, *last, *end;
    njs_string_prop_t  string, split;
    njs_value_t        arguments[3];

    static const njs_value_t  split_key =
                                        njs_wellknown_symbol(NJS_SYMBOL_SPLIT);

    this = njs_argument(args, 0);

    if (njs_slow_path(njs_is_null_or_undefined(this))) {
        njs_type_error(vm, "cannot convert \"%s\"to object",
                       njs_type_string(this->type));
        return NJS_ERROR;
    }

    separator = njs_lvalue_arg(&separator_lvalue, args, nargs, 1);
    value = njs_lvalue_arg(&limit_lvalue, args, nargs, 2);

    if (!njs_is_null_or_undefined(separator)) {
        ret = njs_value_method(vm, separator, njs_value_arg(&split_key),
                               &splitter);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_defined(&splitter)) {
            arguments[0] = *this;
            arguments[1] = *value;

            return njs_function_call(vm, njs_function(&splitter), separator,
                                     arguments, 2, &vm->retval);
        }
    }

    ret = njs_value_to_string(vm, this, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    array = njs_array_alloc(vm, 0, 0, NJS_ARRAY_SPARE);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    limit = UINT32_MAX;

    if (njs_is_defined(value)) {
        ret = njs_value_to_uint32(vm, value, &limit);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    undefined = njs_is_undefined(separator);

    ret = njs_value_to_string(vm, separator, separator);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(limit == 0)) {
        goto done;
    }

    if (njs_slow_path(undefined)) {
        goto single;
    }

    (void) njs_string_prop(&string, this);
    (void) njs_string_prop(&split, separator);

    if (njs_slow_path(string.size == 0)) {
        if (split.size != 0) {
            goto single;
        }

        goto done;
    }

    utf8 = NJS_STRING_BYTE;

    if (string.length != 0) {
        utf8 = NJS_STRING_ASCII;

        if (string.length != string.size) {
            utf8 = NJS_STRING_UTF8;
        }
    }

    start = string.start;
    end = string.start + string.size;
    last = end - split.size;

    do {

        for (p = start; p <= last; p++) {
            if (memcmp(p, split.start, split.size) == 0) {
                goto found;
            }
        }

        p = end;

found:

        next = p + split.size;

        /* Empty split string. */

        if (p == next) {
            p = (utf8 != NJS_STRING_BYTE) ? njs_utf8_next(p, end)
                                          : p + 1;
            next = p;
        }

        size = p - start;

        ret = njs_string_split_part_add(vm, array, utf8, start, size);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        start = next;
        limit--;

    } while (limit != 0 && p < end);

    goto done;

single:

    value = njs_array_push(vm, array);
    if (njs_slow_path(value == NULL)) {
        return NJS_ERROR;
    }

    *value = *this;

done:

    njs_set_array(&vm->retval, array);

    return NJS_OK;
}


njs_int_t
njs_string_split_part_add(njs_vm_t *vm, njs_array_t *array, njs_utf8_t utf8,
    const u_char *start, size_t size)
{
    ssize_t  length;

    length = njs_string_calc_length(utf8, start, size);

    return njs_array_string_add(vm, array, start, size, length);
}


njs_int_t
njs_string_get_substitution(njs_vm_t *vm, njs_value_t *matched,
    njs_value_t *string, int64_t pos, njs_value_t *captures, int64_t ncaptures,
    njs_value_t *groups, njs_value_t *replacement, njs_value_t *retval)
{
    int64_t      tail, size, length, n;
    u_char       c, c2, *p, *r, *end;
    njs_str_t    rep, m, str, cap;
    njs_int_t    ret;
    njs_chb_t    chain;
    njs_value_t  name, value;

    njs_string_get(replacement, &rep);
    p = rep.start;
    end = rep.start + rep.length;

    njs_chb_init(&chain, vm->mem_pool);

    while (p < end) {
        r = njs_strlchr(p, end, '$');
        if (r == NULL || r == &end[-1]) {
            if (njs_fast_path(p == rep.start)) {
                *retval = *replacement;
                return NJS_OK;
            }

            njs_chb_append(&chain, p, end - p);
            goto done;
        }

        njs_chb_append(&chain, p, r - p);
        p = r;

        c = r[1];

        switch (c) {
        case '$':
            njs_chb_append_literal(&chain, "$");
            p += 2;
            break;

        case '&':
            njs_string_get(matched, &m);
            njs_chb_append_str(&chain, &m);
            p += 2;
            break;

        case '`':
            njs_string_get(string, &str);
            njs_chb_append(&chain, str.start, pos);
            p += 2;
            break;

        case '\'':
            njs_string_get(matched, &m);
            tail = pos + m.length;

            njs_string_get(string, &str);
            njs_chb_append(&chain, &str.start[tail],
                           njs_max((int64_t) str.length - tail, 0));
            p += 2;
            break;

        case '<':
            r = njs_strlchr(p, end, '>');
            if (groups == NULL || njs_is_undefined(groups) || r == NULL) {
                njs_chb_append(&chain, p, 2);
                p += 2;
                break;
            }

            p += 2;

            ret = njs_vm_value_string_set(vm, &name, p, r - p);
            if (njs_slow_path(ret != NJS_OK)) {
                goto exception;
            }

            p = r + 1;

            ret = njs_value_property(vm, groups, &name, &value);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto exception;
            }

            if (njs_is_defined(&value)) {
                ret = njs_value_to_string(vm, &value, &value);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto exception;
                }

                njs_string_get(&value, &str);
                njs_chb_append_str(&chain, &str);
            }

            break;

        default:
            if (c >= '0' && c <= '9') {
                n = c - '0';

                c2 = (&r[2] < end) ? r[2] : 0;

                if (c2 >= '0' && c2 <= '9'
                    && (n * 10 + (c2 - '0')) <= ncaptures)
                {
                    n = n * 10 + (c2 - '0');

                } else {
                    c2 = 0;
                }

                if (n == 0 || n > ncaptures) {
                    njs_chb_append(&chain, p, (c2 != 0) ? 3 : 2);
                    p += (c2 != 0) ? 3 : 2;
                    break;
                }

                p += (c2 != 0) ? 3 : 2;

                if (njs_is_defined(&captures[n])) {
                    njs_string_get(&captures[n], &cap);
                    njs_chb_append_str(&chain, &cap);
                }

                break;
            }

            njs_chb_append_literal(&chain, "$");
            p += 1;
            break;
        }
    }

done:

    size = njs_chb_size(&chain);
    if (njs_slow_path(size < 0)) {
        njs_memory_error(vm);
        ret = NJS_ERROR;
        goto exception;
    }

    length = njs_chb_utf8_length(&chain);

    p = njs_string_alloc(vm, retval, size, length);
    if (njs_slow_path(p == NULL)) {
        ret = NJS_ERROR;
        goto exception;
    }

    njs_chb_join_to(&chain, p);

    ret = NJS_OK;

exception:

    njs_chb_destroy(&chain);

    return ret;
}


static njs_int_t
njs_string_prototype_replace(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char             *r;
    size_t             length, size;
    int64_t            pos;
    njs_int_t          ret;
    njs_value_t        *this, *search, *replace;
    njs_value_t        search_lvalue, replace_lvalue, replacer, retval,
                       arguments[3];
    const u_char       *p;
    njs_function_t     *func_replace;
    njs_string_prop_t  string, s, ret_string;

    static const njs_value_t  replace_key =
                                      njs_wellknown_symbol(NJS_SYMBOL_REPLACE);

    this = njs_argument(args, 0);

    if (njs_slow_path(njs_is_null_or_undefined(this))) {
        njs_type_error(vm, "cannot convert \"%s\"to object",
                       njs_type_string(this->type));
        return NJS_ERROR;
    }

    search = njs_lvalue_arg(&search_lvalue, args, nargs, 1);
    replace = njs_lvalue_arg(&replace_lvalue, args, nargs, 2);

    if (!njs_is_null_or_undefined(search)) {
        ret = njs_value_method(vm, search, njs_value_arg(&replace_key),
                               &replacer);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_defined(&replacer)) {
            arguments[0] = *this;
            arguments[1] = *replace;

            return njs_function_call(vm, njs_function(&replacer), search,
                                     arguments, 2, &vm->retval);
        }
    }

    ret = njs_value_to_string(vm, this, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_to_string(vm, search, search);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    func_replace = njs_is_function(replace) ? njs_function(replace) : NULL;

    if (func_replace == NULL) {
        ret = njs_value_to_string(vm, replace, replace);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    (void) njs_string_prop(&string, this);
    (void) njs_string_prop(&s, search);

    pos = njs_string_index_of(&string, &s, 0);
    if (pos < 0) {
        vm->retval = *this;
        return NJS_OK;
    }

    if (func_replace == NULL) {
        ret = njs_string_get_substitution(vm, search, this, pos, NULL, 0, NULL,
                                          replace, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {
        arguments[0] = *search;
        njs_set_number(&arguments[1], pos);
        arguments[2] = *this;

        ret = njs_function_call(vm, func_replace,
                                njs_value_arg(&njs_value_undefined),
                                arguments, 3, &retval);

        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_value_to_string(vm, &retval, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (njs_is_byte_or_ascii_string(&string)) {
        p = string.start + pos;

    } else {
        /* UTF-8 string. */
        p = njs_string_offset(string.start, string.start + string.size, pos);
    }

    (void) njs_string_prop(&ret_string, &retval);

    size = string.size + ret_string.size - s.size;
    length = string.length + ret_string.length - s.length;

    if (njs_is_byte_string(&string)
        || njs_is_byte_string(&s)
        || njs_is_byte_string(&ret_string))
    {
        length = 0;
    }

    r = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    r = njs_cpymem(r, string.start, p - string.start);
    r = njs_cpymem(r, ret_string.start, ret_string.size);
    memcpy(r, p + s.size, string.size - s.size - (p - string.start));

    return NJS_OK;
}


static njs_int_t
njs_string_prototype_iterator_obj(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t kind)
{
    njs_int_t    ret;
    njs_value_t  *this;

    this = njs_argument(args, 0);

    ret = njs_string_object_validate(vm, this);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_array_iterator_create(vm, this, &vm->retval, kind);
}


double
njs_string_to_number(const njs_value_t *value, njs_bool_t parse_float)
{
    double             num;
    njs_bool_t         minus;
    const u_char       *p, *start, *end;
    njs_string_prop_t  string;

    const size_t  infinity = njs_length("Infinity");

    (void) njs_string_trim(value, &string, NJS_TRIM_START);

    p = string.start;
    end = p + string.size;

    if (p == end) {
        return parse_float ? NAN : 0.0;
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
        num = njs_number_hex_parse(&p, end, 0);

    } else {
        start = p;
        num = njs_number_dec_parse(&p, end, 0);

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
njs_string_to_index(const njs_value_t *value)
{
    size_t        size, len;
    double        num;
    njs_bool_t    minus;
    const u_char  *p, *start, *end;
    u_char        buf[128];

    size = value->short_string.size;

    if (size != NJS_STRING_LONG) {
        start = value->short_string.start;

    } else {
        size = value->long_string.size;
        start = value->long_string.data->start;
    }

    p = start;
    end = p + size;
    minus = 0;

    if (size > 1) {
        switch (p[0]) {
        case '0':
            if (size != 1) {
                return NAN;
            }

            /* Fall through. */

        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            break;

        case '-':
            if (size == 2 && p[1] == '0') {
                return -0.0;
            }

            if (size == njs_length("-Infinity")
                && memcmp(&p[1], "Infinity", njs_length("Infinity")) == 0)
            {
                return -INFINITY;
            }

            p++;
            minus = 1;

            break;

        case 'I':
            if (size == njs_length("Infinity")
                && memcmp(p, "Infinity", njs_length("Infinity")) == 0)
            {
                return INFINITY;
            }

            /* Fall through. */

        default:
            return NAN;
        }
    }

    num = njs_strtod(&p, end, 0);
    if (p != end) {
        return NAN;
    }

    num = minus ? -num : num;

    len = njs_dtoa(num, (char *) buf);
    if (size != len || memcmp(start, buf, size) != 0) {
        return NAN;
    }

    return num;
}


/*
 * If string value is null-terminated the corresponding C string
 * is returned as is, otherwise the new copy is allocated with
 * the terminating zero byte.
 */
const char *
njs_string_to_c_string(njs_vm_t *vm, njs_value_t *value)
{
    u_char  *p, *data, *start;
    size_t  size;

    if (value->short_string.size != NJS_STRING_LONG) {
        start = value->short_string.start;
        size = value->short_string.size;

        if (size < NJS_STRING_SHORT) {
            start[size] = '\0';
            return (const char *) start;
        }

    } else {
        start = value->long_string.data->start;
        size = value->long_string.size;
    }

    data = njs_mp_alloc(vm->mem_pool, size + 1);
    if (njs_slow_path(data == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    p = njs_cpymem(data, start, size);
    *p++ = '\0';

    return (const char *) data;
}


static const njs_object_prop_t  njs_string_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 0, 0.0),
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("__proto__"),
        .value = njs_prop_handler(njs_primitive_prototype_get_proto),
        .configurable = 1,
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
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_string_prototype_value_of, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_string_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("concat"),
        .value = njs_native_function(njs_string_prototype_concat, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("fromUTF8"),
        .value = njs_native_function(njs_string_prototype_from_utf8, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toUTF8"),
        .value = njs_native_function(njs_string_prototype_to_utf8, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("fromBytes"),
        .value = njs_native_function(njs_string_prototype_from_bytes, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toBytes"),
        .value = njs_native_function(njs_string_prototype_to_bytes, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("slice"),
        .value = njs_native_function(njs_string_prototype_slice, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("substring"),
        .value = njs_native_function(njs_string_prototype_substring, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("substr"),
        .value = njs_native_function(njs_string_prototype_substr, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("charAt"),
        .value = njs_native_function(njs_string_prototype_char_at, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("charCodeAt"),
        .value = njs_native_function(njs_string_prototype_char_code_at, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("codePointAt"),
        .value = njs_native_function(njs_string_prototype_char_code_at, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("indexOf"),
        .value = njs_native_function(njs_string_prototype_index_of, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("lastIndexOf"),
        .value = njs_native_function(njs_string_prototype_last_index_of, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("includes"),
        .value = njs_native_function(njs_string_prototype_includes, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("startsWith"),
        .value = njs_native_function2(njs_string_prototype_starts_or_ends_with,
                                      1, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("endsWith"),
        .value = njs_native_function2(njs_string_prototype_starts_or_ends_with,
                                      1, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toLowerCase"),
        .value = njs_native_function(njs_string_prototype_to_lower_case, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toUpperCase"),
        .value = njs_native_function(njs_string_prototype_to_upper_case, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("trim"),
        .value = njs_native_function2(njs_string_prototype_trim, 0,
                                      NJS_TRIM_START | NJS_TRIM_END),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("trimStart"),
        .value = njs_native_function2(njs_string_prototype_trim, 0,
                                      NJS_TRIM_START),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("trimEnd"),
        .value = njs_native_function2(njs_string_prototype_trim, 0,
                                      NJS_TRIM_END),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("repeat"),
        .value = njs_native_function(njs_string_prototype_repeat, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("padStart"),
        .value = njs_native_function2(njs_string_prototype_pad, 1, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("padEnd"),
        .value = njs_native_function2(njs_string_prototype_pad, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("search"),
        .value = njs_native_function(njs_string_prototype_search, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("match"),
        .value = njs_native_function(njs_string_prototype_match, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("split"),
        .value = njs_native_function(njs_string_prototype_split, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("replace"),
        .value = njs_native_function(njs_string_prototype_replace, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_ITERATOR),
        .value = njs_native_function2(njs_string_prototype_iterator_obj, 0,
                                      NJS_ENUM_VALUES),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_string_prototype_init = {
    njs_string_prototype_properties,
    njs_nitems(njs_string_prototype_properties),
};


const njs_object_prop_t  njs_string_instance_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("length"),
        .value = njs_prop_handler(njs_string_instance_length),
    },
};


const njs_object_init_t  njs_string_instance_init = {
    njs_string_instance_properties,
    njs_nitems(njs_string_instance_properties),
};


njs_int_t
njs_string_encode_uri(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t component)
{
    u_char                byte, *dst;
    uint64_t              size;
    uint32_t              cp, cp_low;
    njs_int_t             ret;
    njs_value_t           *value;
    const u_char          *src, *end;
    const uint32_t        *escape;
    njs_string_prop_t     string;
    njs_unicode_decode_t  ctx;
    u_char                encode[4];

    static const uint32_t  escape_uri[] = {
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

    static const uint32_t  escape_uri_component[] = {
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

    if (nargs < 2) {
        vm->retval = njs_string_undefined;
        return NJS_OK;
    }

    value = njs_argument(args, 1);
    ret = njs_value_to_string(vm, value, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    escape = (component) ? escape_uri_component : escape_uri;

    njs_prefetch(escape);

    (void) njs_string_prop(&string, value);

    size = 0;
    src = string.start;
    end = src + string.size;

    if (njs_is_byte_or_ascii_string(&string)) {

        while (src < end) {
            byte = *src++;
            size += njs_need_escape(escape, byte) ? 3 : 1;
        }

    } else {
        /* UTF-8 string. */

        njs_utf8_decode_init(&ctx);

        while (src < end) {
            cp = njs_utf8_decode(&ctx, &src, end);

            if (cp < 0x80 && !njs_need_escape(escape, cp)) {
                size++;
                continue;
            }

            if (njs_slow_path(njs_surrogate_any(cp))) {
                if (src == end) {
                    goto uri_error;
                }

                if (njs_surrogate_leading(cp)) {
                    cp_low = njs_utf8_decode(&ctx, &src, end);

                    if (njs_slow_path(!njs_surrogate_trailing(cp_low))) {
                        goto uri_error;
                    }

                    cp = njs_surrogate_pair(cp, cp_low);
                    size += njs_utf8_size(cp) * 3;
                    continue;
                }

                goto uri_error;
            }

            size += njs_utf8_size(cp) * 3;
        }
    }

    if (size == 0) {
        /* GC: retain src. */
        vm->retval = *value;
        return NJS_OK;
    }

    dst = njs_string_alloc(vm, &vm->retval, size, size);
    if (njs_slow_path(dst == NULL)) {
        return NJS_ERROR;
    }

    src = string.start;

    if (njs_is_byte_or_ascii_string(&string)) {
        (void) njs_string_encode(escape, string.size, src, dst);
        return NJS_OK;
    }

    /* UTF-8 string. */

    njs_utf8_decode_init(&ctx);

    while (src < end) {
        cp = njs_utf8_decode(&ctx, &src, end);

        if (njs_slow_path(njs_surrogate_leading(cp))) {
            cp_low = njs_utf8_decode(&ctx, &src, end);
            cp = njs_surrogate_pair(cp, cp_low);
        }

        njs_utf8_encode(encode, cp);

        dst = njs_string_encode(escape, njs_utf8_size(cp), encode, dst);
    }

    return NJS_OK;

uri_error:

    njs_uri_error(vm, "malformed URI");

    return NJS_ERROR;
}


njs_inline uint32_t
njs_string_decode_uri_cp(const int8_t *hex, const u_char **start,
    const u_char *end, njs_bool_t expect_percent)
{
    int8_t                d0, d1;
    uint32_t              cp;
    const u_char          *p;
    njs_unicode_decode_t  ctx;

    njs_utf8_decode_init(&ctx);

    cp = njs_utf8_decode(&ctx, start, end);
    if (njs_fast_path(cp != '%')) {
        return expect_percent ? NJS_UNICODE_ERROR : cp;
    }

    p = *start;

    if (njs_slow_path((p + 1) >= end)) {
        return NJS_UNICODE_ERROR;
    }

    d0 = hex[*p++];
    if (njs_slow_path(d0 < 0)) {
        return NJS_UNICODE_ERROR;
    }

    d1 = hex[*p++];
    if (njs_slow_path(d1 < 0)) {
        return NJS_UNICODE_ERROR;
    }

    *start += 2;

    return (d0 << 4) + d1;
}


njs_inline njs_bool_t
njs_reserved(const uint32_t *reserve, uint32_t byte)
{
    return ((reserve[byte >> 5] & ((uint32_t) 1 << (byte & 0x1f))) != 0);
}


njs_int_t
njs_string_decode_uri(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t component)
{
    u_char                *dst;
    int64_t               size, length;
    uint32_t              cp;
    njs_int_t             ret;
    njs_chb_t             chain;
    njs_uint_t            i, n;
    njs_bool_t            percent;
    njs_value_t           *value;
    const u_char          *src, *p, *end;
    const uint32_t        *reserve;
    njs_string_prop_t     string;
    njs_unicode_decode_t  ctx;
    u_char                encode[4];

    static const uint32_t  reserve_uri[] = {
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

    static const uint32_t  reserve_uri_component[] = {
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

    static const int8_t  hex[256]
        njs_aligned(32) =
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

    if (nargs < 2) {
        vm->retval = njs_string_undefined;
        return NJS_OK;
    }

    value = njs_argument(args, 1);
    ret = njs_value_to_string(vm, value, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    reserve = component ? reserve_uri_component : reserve_uri;

    njs_prefetch(reserve);
    njs_prefetch(&hex['0']);

    (void) njs_string_prop(&string, value);

    length = 0;
    src = string.start;
    end = string.start + string.size;

    njs_chb_init(&chain, vm->mem_pool);

    njs_utf8_decode_init(&ctx);

    while (src < end) {
        percent = (src[0] == '%');
        cp = njs_string_decode_uri_cp(hex, &src, end, 0);
        if (njs_slow_path(cp > NJS_UNICODE_MAX_CODEPOINT)) {
            goto uri_error;
        }

        if (!percent) {
            length += 1;
            dst = njs_chb_reserve(&chain, 4);
            if (dst != NULL) {
                njs_utf8_encode(dst, cp);
                njs_chb_written(&chain, njs_utf8_size(cp));
            }

            continue;
        }

        if (cp < 0x80) {
            if (njs_reserved(reserve, cp)) {
                length += 3;
                njs_chb_append(&chain, &src[-3], 3);

            } else {
                length += 1;
                dst = njs_chb_reserve(&chain, 1);
                if (dst != NULL) {
                    *dst = cp;
                    njs_chb_written(&chain, 1);
                }
            }

            continue;
        }

        n = 1;

        do {
            n++;
        } while (((cp << n) & 0x80));

        if (njs_slow_path(n > 4)) {
            goto uri_error;
        }

        encode[0] = cp;

        for (i = 1; i < n; i++) {
            cp = njs_string_decode_uri_cp(hex, &src, end, 1);
            if (njs_slow_path(cp > NJS_UNICODE_MAX_CODEPOINT)) {
                goto uri_error;
            }

            encode[i] = cp;
        }

        p = encode;
        cp = njs_utf8_decode(&ctx, &p, p + n);
        if (njs_slow_path(cp > NJS_UNICODE_MAX_CODEPOINT)) {
            goto uri_error;
        }

        dst = njs_chb_reserve(&chain, 4);
        if (dst != NULL) {
            njs_utf8_encode(dst, cp);
            njs_chb_written(&chain, njs_utf8_size(cp));
        }

        length += 1;
    }

    size = njs_chb_size(&chain);
    if (njs_slow_path(size < 0)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    if (size == 0) {
        /* GC: retain src. */
        vm->retval = *value;
        return NJS_OK;
    }

    dst = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(dst == NULL)) {
        return NJS_ERROR;
    }

    njs_chb_join_to(&chain, dst);
    njs_chb_destroy(&chain);

    return NJS_OK;

uri_error:

    njs_uri_error(vm, "malformed URI");

    return NJS_ERROR;
}


njs_int_t
njs_string_btoa(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char                *dst;
    size_t                len, length;
    uint32_t              cp0, cp1, cp2;
    njs_int_t             ret;
    njs_value_t           *value, lvalue;
    const u_char          *p, *end;
    njs_string_prop_t     string;
    njs_unicode_decode_t  ctx;

    value = njs_lvalue_arg(&lvalue, args, nargs, 1);

    ret = njs_value_to_string(vm, value, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    len = njs_string_prop(&string, value);

    p = string.start;
    end = string.start + string.size;

    njs_utf8_decode_init(&ctx);

    length = njs_base64_encoded_length(len);

    dst = njs_string_alloc(vm, &vm->retval, length, length);
    if (njs_slow_path(dst == NULL)) {
        return NJS_ERROR;
    }

    while (len > 2 && p < end) {
        cp0 = njs_utf8_decode(&ctx, &p, end);
        cp1 = njs_utf8_decode(&ctx, &p, end);
        cp2 = njs_utf8_decode(&ctx, &p, end);

        if (njs_slow_path(cp0 > 0xff || cp1 > 0xff || cp2 > 0xff)) {
            goto error;
        }

        *dst++ = njs_basis64_enc[cp0 >> 2];
        *dst++ = njs_basis64_enc[((cp0 & 0x03) << 4) | (cp1 >> 4)];
        *dst++ = njs_basis64_enc[((cp1 & 0x0f) << 2) | (cp2 >> 6)];
        *dst++ = njs_basis64_enc[cp2 & 0x3f];

        len -= 3;
    }

    if (len > 0) {
        cp0 = njs_utf8_decode(&ctx, &p, end);
        if (njs_slow_path(cp0 > 0xff)) {
            goto error;
        }

        *dst++ = njs_basis64_enc[cp0 >> 2];

        if (len == 1) {
            *dst++ = njs_basis64_enc[(cp0 & 0x03) << 4];
            *dst++ = '=';
            *dst++ = '=';

        } else {
            cp1 = njs_utf8_decode(&ctx, &p, end);
            if (njs_slow_path(cp1 > 0xff)) {
                goto error;
            }

            *dst++ = njs_basis64_enc[((cp0 & 0x03) << 4) | (cp1 >> 4)];
            *dst++ = njs_basis64_enc[(cp1 & 0x0f) << 2];
            *dst++ = '=';
        }

    }

    return NJS_OK;

error:

    njs_type_error(vm, "invalid character (>= U+00FF)");

    return NJS_ERROR;
}


njs_inline void
njs_chb_write_byte_as_utf8(njs_chb_t *chain, u_char byte)
{
    njs_utf8_encode(njs_chb_current(chain), byte);
    njs_chb_written(chain, njs_utf8_size(byte));
}


njs_int_t
njs_string_atob(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t        i, n, len, pad;
    u_char        *dst, *tmp, *p;
    ssize_t       size;
    njs_str_t     str;
    njs_int_t     ret;
    njs_chb_t     chain;
    njs_value_t   *value, lvalue;
    const u_char  *b64, *s;

    value = njs_lvalue_arg(&lvalue, args, nargs, 1);

    ret = njs_value_to_string(vm, value, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    /* Forgiving-base64 decode. */

    b64 = njs_basis64;
    njs_string_get(value, &str);

    tmp = njs_mp_alloc(vm->mem_pool, str.length);
    if (tmp == NULL) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    p = tmp;

    for (i = 0; i < str.length; i++) {
        if (njs_slow_path(str.start[i] == ' ')) {
            continue;
        }

        *p++ = str.start[i];
    }

    pad = 0;
    str.start = tmp;
    str.length = p - tmp;

    if (str.length % 4 == 0) {
        if (str.length > 0) {
            if (str.start[str.length - 1] == '=') {
                pad += 1;
            }

            if (str.start[str.length - 2] == '=') {
                pad += 1;
            }
        }

    } else if (str.length % 4 == 1) {
        goto error;
    }

    for (i = 0; i < str.length - pad; i++) {
        if (njs_slow_path(b64[str.start[i]] == 77)) {
            goto error;
        }
    }

    len = njs_base64_decoded_length(str.length, pad);

    njs_chb_init(&chain, vm->mem_pool);

    dst = njs_chb_reserve(&chain, len * 2);
    if (njs_slow_path(dst == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    n = len;
    s = str.start;

    while (n >= 3) {
        njs_chb_write_byte_as_utf8(&chain, b64[s[0]] << 2 | b64[s[1]] >> 4);
        njs_chb_write_byte_as_utf8(&chain, b64[s[1]] << 4 | b64[s[2]] >> 2);
        njs_chb_write_byte_as_utf8(&chain, b64[s[2]] << 6 | b64[s[3]]);

        s += 4;
        n -= 3;
    }

    if (n >= 1) {
        njs_chb_write_byte_as_utf8(&chain, b64[s[0]] << 2 | b64[s[1]] >> 4);
    }

    if (n >= 2) {
        njs_chb_write_byte_as_utf8(&chain, b64[s[1]] << 4 | b64[s[2]] >> 2);
    }

    size = njs_chb_size(&chain);
    if (njs_slow_path(size < 0)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    if (size == 0) {
        njs_value_assign(&vm->retval, &njs_string_empty);
        return NJS_OK;
    }

    dst = njs_string_alloc(vm, &vm->retval, size, len);
    if (njs_slow_path(dst == NULL)) {
        return NJS_ERROR;
    }

    njs_chb_join_to(&chain, dst);
    njs_chb_destroy(&chain);

    njs_mp_free(vm->mem_pool, tmp);

    return NJS_OK;

error:

    njs_type_error(vm, "the string to be decoded is not correctly encoded");

    return NJS_ERROR;
}


const njs_object_type_init_t  njs_string_type_init = {
    .constructor = njs_native_ctor(njs_string_constructor, 1, 0),
    .constructor_props = &njs_string_constructor_init,
    .prototype_props = &njs_string_prototype_init,
    .prototype_value = { .object_value = {
                            .value = njs_string(""),
                            .object = { .type = NJS_OBJECT_VALUE } }
                       },
};
