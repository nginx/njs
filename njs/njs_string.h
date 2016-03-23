
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_STRING_H_INCLUDED_
#define _NJS_STRING_H_INCLUDED_


/*
 * nJSVM supports two string variants:
 *
 * 1) short strings which size is lesser than 14 bytes, these strings are
 *    stored inside njs_value_t (see njs_vm.h for details);
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
 * Should be power of two to use shift and binary and operations instead of
 * division and remainder operations but no less than 16 because the maximum
 * length of short string inlined in njs_value_t is less than 16 bytes.
 */
#define NJS_STRING_MAP_OFFSET  32

/*
 * The JavaScript standard states that strings are stored in UTF-16.
 * nJSVM allows to store any byte sequences in strings.  A size of the
 * string in bytes is stored in the size field.  If a byte sequence is
 * valid UTF-8 string then its length is stored in the UTF-8 length field.
 * Otherwise, the length field is zero.  If a string is UTF-8 string then
 * string functions work with UTF-8 characters positions and lengths.
 * Othersise they work with byte positions and lengths.  Using UTF-8
 * encoding does not allow to get quickly a character at specified position.
 * To speed up this search a map of offsets is stored after the UTF-8 string.
 * The map is aligned to uint32_t and contains byte positions of each
 * NJS_STRING_MAP_OFFSET UTF-8 character except zero position.  The map
 * can be allocated and updated on demand.  If a string come outside
 * JavaScript as byte sequnece just to be concatenated or to be used in
 * regular expressions the offset map is not required.
 *
 * The map is not allocated:
 * 1) if the length is zero hence it is a byte string;
 * 2) if the size and length are equal so the string contains only ASCII
 *    characters map is not required;
 * 3) if the length is less than NJS_STRING_MAP_OFFSET.
 *
 * The current implementation does not support Unicode surrogate pairs.
 * If offset in map points to surrogate pair then the previous offset
 * should be used and so on until start of the string.
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


u_char *njs_string_alloc(njs_vm_t *vm, njs_value_t *value, uint32_t size,
    uint32_t length)
    NXT_MALLOC_LIKE;
void njs_string_copy(njs_value_t *dst, njs_value_t *src);
njs_ret_t njs_string_validate(njs_vm_t *vm, njs_string_prop_t *string,
    njs_value_t *value);
nxt_noinline size_t njs_string_prop(njs_string_prop_t *string,
    njs_value_t *value);
njs_ret_t njs_string_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
void njs_string_offset_map_init(const u_char *start, size_t size);
nxt_bool_t njs_string_eq(const njs_value_t *val1, const njs_value_t *val2);
nxt_int_t njs_string_cmp(const njs_value_t *val1, const njs_value_t *val2);
njs_ret_t njs_string_slice(njs_vm_t *vm, njs_value_t *dst,
    const njs_string_prop_t *string, njs_slice_prop_t *slice);
const u_char *njs_string_offset(const u_char *start, const u_char *end,
    size_t index);
nxt_noinline uint32_t njs_string_index(njs_string_prop_t *string,
    uint32_t offset);
njs_ret_t njs_primitive_value_to_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *src);
double njs_string_to_number(njs_value_t *value);

njs_index_t njs_value_index(njs_vm_t *vm, njs_parser_t *parser,
    const njs_value_t *src);

extern const njs_object_init_t  njs_string_constructor_init;
extern const njs_object_init_t  njs_string_prototype_init;


#endif /* _NJS_STRING_H_INCLUDED_ */
