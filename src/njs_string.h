
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_STRING_H_INCLUDED_
#define _NJS_STRING_H_INCLUDED_


/*
 * nJSVM supports two string variants:
 *
 * 1) short strings which size is less than or equal to 14 (NJS_STRING_SHORT)
 *    bytes, these strings are stored inside  njs_value_t (see njs_vm.h for
 *    details);
 *
 * 2) and long strings using additional njs_string_t structure.
 *    This structure has the start field to support external strings.
 *    The long strings can have optional UTF-8 offset map.
 *
 * The number of the string variants is limited to 2 variants to minimize
 * overhead of processing string fields.
 */

/* The maximum signed int32_t. */
#define NJS_STRING_MAX_LENGTH  0x7fffffff

/*
 * NJS_STRING_MAP_STRIDE should be power of two to use shift and binary
 * AND operations instead of division and remainder operations but no
 * less than 16 because the maximum length of short string inlined in
 * njs_value_t is less than 16 bytes.
 */
#define NJS_STRING_MAP_STRIDE  32

#define njs_string_map_offset(size)  njs_align_size((size), sizeof(uint32_t))

#define njs_string_map_start(p)                                               \
    ((uint32_t *) njs_align_ptr((p), sizeof(uint32_t)))

#define njs_string_map_size(length)                                           \
    (((length - 1) / NJS_STRING_MAP_STRIDE) * sizeof(uint32_t))

/*
 * ECMAScript strings are stored in UTF-16.  nJSVM however, allows to store
 * any byte sequences in strings.  A size of string in bytes is stored in the
 * size field.  If byte sequence is valid UTF-8 string then its length is
 * stored in the UTF-8 length field.  Otherwise, the length field is zero.
 * If a string is UTF-8 string then string functions use UTF-8 characters
 * positions and lengths.  Otherwise they use with byte positions and lengths.
 * Using UTF-8 encoding does not allow to get quickly a character at specified
 * position.  To speed up this search a map of offsets is stored after the
 * UTF-8 string.  The map is aligned to uint32_t and contains byte positions
 * of each NJS_STRING_MAP_STRIDE UTF-8 character except zero position.  The
 * map can be initialized on demand.  Unitialized map is marked with zero
 * value in the first map element.  If string comes outside JavaScript as
 * byte string just to be concatenated or to match regular expressions the
 * offset map is not required.
 *
 * The map is not allocated:
 * 1) if string length is zero hence string is a byte string;
 * 2) if string size and length are equal so the string contains only
 *    ASCII characters and map is not required;
 * 3) if string length is less than NJS_STRING_MAP_STRIDE.
 *
 * The current implementation does not support Unicode surrogate pairs.
 * It can be implemented later if it will be required using the following
 * algorithm: if offset in map points to surrogate pair then the previous
 * offset should be used and so on until start of the string.
 */

struct njs_string_s {
    u_char    *start;
    uint32_t  length;   /* Length in UTF-8 characters. */
    uint32_t  retain;   /* Link counter. */
};


typedef struct {
    size_t    size;
    size_t    length;
    u_char    *start;
} njs_string_prop_t;


typedef struct {
    size_t    start;
    size_t    length;
    size_t    string_length;
} njs_slice_prop_t;


typedef enum {
    NJS_STRING_BYTE = 0,
    NJS_STRING_ASCII,
    NJS_STRING_UTF8,
} njs_utf8_t;


typedef enum {
    NJS_TRIM_START = 1,
    NJS_TRIM_END = 2,
} njs_trim_t;


njs_inline njs_bool_t
njs_is_byte_string(njs_string_prop_t *string)
{
    return (string->length == 0 && string->size != 0);
}


njs_inline njs_bool_t
njs_is_byte_or_ascii_string(njs_string_prop_t *string)
{
    return (string->length == 0 || string->length == string->size);
}


njs_inline uint32_t
njs_string_calc_length(njs_utf8_t utf8, const u_char *start, size_t size)
{
    ssize_t  length;

    switch (utf8) {

    case NJS_STRING_BYTE:
        return 0;

    case NJS_STRING_ASCII:
        return size;

    case NJS_STRING_UTF8:
    default:
        length = njs_utf8_length(start, size);

        return (length >= 0) ? length : 0;
    }
}


njs_inline uint32_t
njs_string_length(njs_value_t *string)
{
    uint32_t  length, size;

    if (string->short_string.size != NJS_STRING_LONG) {
        size = string->short_string.size;
        length = string->short_string.length;

    } else {
        size = string->long_string.size;
        length = string->long_string.data->length;
    }

    return (length == 0) ? size : length;
}


njs_inline njs_bool_t
njs_need_escape(const uint32_t *escape, uint32_t byte)
{
    return ((escape[byte >> 5] & ((uint32_t) 1 << (byte & 0x1f))) != 0);
}


njs_inline u_char *
njs_string_encode(const uint32_t *escape, size_t size, const u_char *src,
    u_char *dst)
{
    uint8_t              byte;
    static const u_char  hex[16] = "0123456789ABCDEF";

    do {
        byte = *src++;

        if (njs_need_escape(escape, byte)) {
            *dst++ = '%';
            *dst++ = hex[byte >> 4];
            *dst++ = hex[byte & 0xf];

        } else {
            *dst++ = byte;
        }

        size--;

    } while (size != 0);

    return dst;
}


njs_int_t njs_string_set(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size);
u_char *njs_string_alloc(njs_vm_t *vm, njs_value_t *value, uint64_t size,
    uint64_t length);
njs_int_t njs_string_new(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size, uint32_t length);
njs_int_t njs_string_create(njs_vm_t *vm, njs_value_t *value, const char *src,
    size_t size);

void njs_encode_hex(njs_str_t *dst, const njs_str_t *src);
size_t njs_encode_hex_length(const njs_str_t *src, size_t *out_size);
void njs_encode_base64(njs_str_t *dst, const njs_str_t *src);
size_t njs_encode_base64_length(const njs_str_t *src, size_t *out_size);

void njs_decode_utf8(njs_str_t *dst, const njs_str_t *src);
size_t njs_decode_utf8_length(const njs_str_t *src, size_t *out_size);
void njs_decode_hex(njs_str_t *dst, const njs_str_t *src);
size_t njs_decode_hex_length(const njs_str_t *src, size_t *out_size);
void njs_decode_base64(njs_str_t *dst, const njs_str_t *src);
size_t njs_decode_base64_length(const njs_str_t *src, size_t *out_size);
void njs_decode_base64url(njs_str_t *dst, const njs_str_t *src);
size_t njs_decode_base64url_length(const njs_str_t *src, size_t *out_size);

njs_int_t njs_string_hex(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);
njs_int_t njs_string_base64(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);
njs_int_t njs_string_base64url(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);
njs_int_t njs_string_decode_utf8(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);
njs_int_t njs_string_decode_hex(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);
njs_int_t njs_string_decode_base64(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);
njs_int_t njs_string_decode_base64url(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);
void njs_string_truncate(njs_value_t *value, uint32_t size, uint32_t length);
uint32_t njs_string_trim(const njs_value_t *value, njs_string_prop_t *string,
    unsigned mode);
void njs_string_copy(njs_value_t *dst, njs_value_t *src);
njs_int_t njs_string_validate(njs_vm_t *vm, njs_string_prop_t *string,
    njs_value_t *value);
size_t njs_string_prop(njs_string_prop_t *string, const njs_value_t *value);
njs_int_t njs_string_cmp(const njs_value_t *val1, const njs_value_t *val2);
void njs_string_slice_string_prop(njs_string_prop_t *dst,
    const njs_string_prop_t *string, const njs_slice_prop_t *slice);
njs_int_t njs_string_slice(njs_vm_t *vm, njs_value_t *dst,
    const njs_string_prop_t *string, const njs_slice_prop_t *slice);
const u_char *njs_string_offset(const u_char *start, const u_char *end,
    size_t index);
uint32_t njs_string_index(njs_string_prop_t *string, uint32_t offset);
void njs_string_offset_map_init(const u_char *start, size_t size);
double njs_string_to_index(const njs_value_t *value);
const char *njs_string_to_c_string(njs_vm_t *vm, njs_value_t *value);
njs_int_t njs_string_encode_uri(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t component);
njs_int_t njs_string_decode_uri(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t component);
njs_int_t njs_string_btoa(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused);
njs_int_t njs_string_atob(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused);

njs_int_t njs_string_prototype_concat(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_int_t njs_string_split_part_add(njs_vm_t *vm, njs_array_t *array,
    njs_utf8_t utf8, const u_char *start, size_t size);
njs_int_t njs_string_get_substitution(njs_vm_t *vm, njs_value_t *matched,
    njs_value_t *string, int64_t pos, njs_value_t *captures, int64_t ncaptures,
    njs_value_t *groups, njs_value_t *replacement, njs_value_t *retval);


extern const njs_object_init_t  njs_string_instance_init;
extern const njs_object_type_init_t  njs_string_type_init;


#endif /* _NJS_STRING_H_INCLUDED_ */
