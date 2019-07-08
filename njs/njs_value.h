
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VALUE_H_INCLUDED_
#define _NJS_VALUE_H_INCLUDED_


#include <nxt_trace.h>
#include <nxt_queue.h>
#include <nxt_regex.h>
#include <nxt_random.h>
#include <nxt_djb_hash.h>
#include <nxt_mp.h>

#include <math.h>


/*
 * The order of the enum is used in njs_vmcode_typeof()
 * and njs_object_prototype_to_string().
 */

typedef enum {
    NJS_NULL                  = 0x00,
    NJS_UNDEFINED             = 0x01,

    /* The order of the above type is used in njs_is_null_or_undefined(). */

    NJS_BOOLEAN               = 0x02,
    /*
     * The order of the above type is used in
     * njs_is_null_or_undefined_or_boolean().
     */
    NJS_NUMBER                = 0x03,
    /*
     * The order of the above type is used in njs_is_numeric().
     * Booleans, null and void values can be used in mathematical operations:
     *   a numeric value of the true value is one,
     *   a numeric value of the null and false values is zero,
     *   a numeric value of the void value is NaN.
     */
    NJS_STRING                = 0x04,

    /* The order of the above type is used in njs_is_primitive(). */

    NJS_DATA                  = 0x05,

    /* The type is external code. */
    NJS_EXTERNAL              = 0x06,

    /*
     * The invalid value type is used:
     *   for uninitialized array members,
     *   to detect non-declared explicitly or implicitly variables,
     *   for native property getters.
     */
    NJS_INVALID               = 0x07,

    /*
     * The object types are >= NJS_OBJECT, this is used in njs_is_object().
     * NJS_OBJECT_BOOLEAN, NJS_OBJECT_NUMBER, and NJS_OBJECT_STRING must be
     * in the same order as NJS_BOOLEAN, NJS_NUMBER, and NJS_STRING.  It is
     * used in njs_primitive_prototype_index().  The order of object types
     * is used in vm->prototypes and vm->constructors arrays.
     */
    NJS_OBJECT                = 0x10,
    NJS_ARRAY                 = 0x11,
    NJS_OBJECT_BOOLEAN        = 0x12,
    NJS_OBJECT_NUMBER         = 0x13,
    NJS_OBJECT_STRING         = 0x14,
    NJS_FUNCTION              = 0x15,
    NJS_REGEXP                = 0x16,
    NJS_DATE                  = 0x17,
    NJS_OBJECT_ERROR          = 0x18,
    NJS_OBJECT_EVAL_ERROR     = 0x19,
    NJS_OBJECT_INTERNAL_ERROR = 0x1a,
    NJS_OBJECT_RANGE_ERROR    = 0x1b,
    NJS_OBJECT_REF_ERROR      = 0x1c,
    NJS_OBJECT_SYNTAX_ERROR   = 0x1d,
    NJS_OBJECT_TYPE_ERROR     = 0x1e,
    NJS_OBJECT_URI_ERROR      = 0x1f,
    NJS_OBJECT_VALUE          = 0x20,
#define NJS_TYPE_MAX         (NJS_OBJECT_VALUE + 1)
} njs_value_type_t;


/*
 * njs_prop_handler_t operates as a property getter and/or setter.
 * The handler receives NULL setval if it is invoked in GET context and
 * non-null otherwise.
 *
 * njs_prop_handler_t is expected to return:
 *   NXT_OK - handler executed successfully;
 *   NXT_ERROR - some error, vm->retval contains appropriate exception;
 *   NXT_DECLINED - handler was applied to inappropriate object.
 */
typedef njs_ret_t (*njs_prop_handler_t) (njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
typedef njs_ret_t (*njs_function_native_t) (njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t retval);


typedef struct njs_string_s           njs_string_t;
typedef struct njs_object_s           njs_object_t;
typedef struct njs_object_value_s     njs_object_value_t;
typedef struct njs_function_lambda_s  njs_function_lambda_t;
typedef struct njs_regexp_pattern_s   njs_regexp_pattern_t;
typedef struct njs_array_s            njs_array_t;
typedef struct njs_regexp_s           njs_regexp_t;
typedef struct njs_date_s             njs_date_t;
typedef struct njs_property_next_s    njs_property_next_t;
typedef struct njs_object_init_s      njs_object_init_t;


union njs_value_s {
    /*
     * The njs_value_t size is 16 bytes and must be aligned to 16 bytes
     * to provide 4 bits to encode scope in njs_index_t.  This space is
     * used to store short strings.  The maximum size of a short string
     * is 14 (NJS_STRING_SHORT).  If the short_string.size field is 15
     * (NJS_STRING_LONG) then the size is in the long_string.size field
     * and the long_string.data field points to a long string.
     *
     * The number of the string types is limited to 2 types to minimize
     * overhead of processing string fields.  It is also possible to add
     * strings with size from 14 to 254 which size and length are stored in
     * the string_size and string_length byte wide fields.  This will lessen
     * the maximum size of short string to 13.
     */
    struct {
        njs_value_type_t              type:8;  /* 6 bits */
        /*
         * The truth field is set during value assignment and then can be
         * quickly tested by logical and conditional operations regardless
         * of value type.  The truth field coincides with short_string.size
         * and short_string.length so when string size and length are zero
         * the string's value is false.
         */
        uint8_t                       truth;

        uint16_t                      _spare1;
        uint32_t                      _spare2;

        union {
            double                    number;
            njs_object_t              *object;
            njs_array_t               *array;
            njs_object_value_t        *object_value;
            njs_function_t            *function;
            njs_function_lambda_t     *lambda;
            njs_regexp_t              *regexp;
            njs_date_t                *date;
            njs_prop_handler_t        prop_handler;
            njs_value_t               *value;
            njs_property_next_t       *next;
            void                      *data;
        } u;
    } data;

    struct {
        njs_value_type_t              type:8;  /* 6 bits */

#define NJS_STRING_SHORT              14
#define NJS_STRING_LONG               15

        uint8_t                       size:4;
        uint8_t                       length:4;

        u_char                        start[NJS_STRING_SHORT];
    } short_string;

    struct {
        njs_value_type_t              type:8;  /* 6 bits */
        uint8_t                       truth;

        /* 0xff if data is external string. */
        uint8_t                       external;
        uint8_t                       _spare;

        uint32_t                      size;
        njs_string_t                  *data;
    } long_string;

    struct {
        njs_value_type_t              type:8;  /* 6 bits */
        uint8_t                       truth;

        uint16_t                      _spare;

        uint32_t                      index;
        const njs_extern_t            *proto;
    } external;

    njs_value_type_t                  type:8;  /* 6 bits */
};


struct njs_object_s {
    /* A private hash of njs_object_prop_t. */
    nxt_lvlhsh_t                      hash;

    /* A shared hash of njs_object_prop_t. */
    nxt_lvlhsh_t                      shared_hash;

    /* An object __proto__. */
    njs_object_t                      *__proto__;

    /* The type is used in constructor prototypes. */
    njs_value_type_t                  type:8;
    uint8_t                           shared;     /* 1 bit */
    uint8_t                           extensible; /* 1 bit */
};


struct njs_object_value_s {
    njs_object_t                      object;
    /* The value can be unaligned since it never used in nJSVM operations. */
    njs_value_t                       value;
};


struct njs_array_s {
    njs_object_t                      object;
    uint32_t                          size;
    uint32_t                          length;
    njs_value_t                       *start;
    njs_value_t                       *data;
};


typedef struct {
    union {
        uint32_t                      count;
        njs_value_t                   values;
    } u;

    njs_value_t                       values[1];
} njs_closure_t;


#define NJS_ARGS_TYPES_MAX            5

struct njs_function_s {
    njs_object_t                      object;

    uint8_t                           args_types[NJS_ARGS_TYPES_MAX];
    uint8_t                           args_offset;
    uint8_t                           continuation_size;

    /* Function is a closure. */
    uint8_t                           closure:1;

    uint8_t                           native:1;
    uint8_t                           ctor:1;

    union {
        njs_function_lambda_t         *lambda;
        njs_function_native_t         native;
    } u;

    njs_value_t                       *bound;
#if (NXT_SUNC)
    njs_closure_t                     *closures[1];
#else
    njs_closure_t                     *closures[];
#endif
};


struct njs_regexp_s {
    njs_object_t                      object;
    uint32_t                          last_index;
    njs_regexp_pattern_t              *pattern;
    /*
     * This string value can be unaligned since
     * it never used in nJSVM operations.
     */
    njs_value_t                       string;
};


struct njs_date_s {
    njs_object_t                      object;
    double                            time;
};


typedef union {
    njs_object_t                      object;
    njs_object_value_t                object_value;
    njs_array_t                       array;
    njs_function_t                    function;
    njs_regexp_t                      regexp;
    njs_date_t                        date;
} njs_object_prototype_t;


typedef enum {
    NJS_ENUM_KEYS,
    NJS_ENUM_VALUES,
    NJS_ENUM_BOTH,
} njs_object_enum_t;


#define njs_value(_type, _truth, _number) {                                   \
    .data = {                                                                 \
        .type = _type,                                                        \
        .truth = _truth,                                                      \
        .u.number = _number,                                                  \
    }                                                                         \
}


#define njs_string(s) {                                                       \
    .short_string = {                                                         \
        .type = NJS_STRING,                                                   \
        .size = nxt_length(s),                                                \
        .length = nxt_length(s),                                              \
        .start = s,                                                           \
    }                                                                         \
}


/* NJS_STRING_LONG is set for both big and little endian platforms. */

#define njs_long_string(s) {                                                  \
    .long_string = {                                                          \
        .type = NJS_STRING,                                                   \
        .truth = (NJS_STRING_LONG << 4) | NJS_STRING_LONG,                    \
        .size = nxt_length(s),                                                \
        .data = & (njs_string_t) {                                            \
            .start = (u_char *) s,                                            \
            .length = nxt_length(s),                                          \
        }                                                                     \
    }                                                                         \
}


#define njs_native_function(_function, _size, ...) {                          \
    .data = {                                                                 \
        .type = NJS_FUNCTION,                                                 \
        .truth = 1,                                                           \
        .u.function = & (njs_function_t) {                                    \
            .native = 1,                                                      \
            .continuation_size = _size,                                       \
            .args_types = { __VA_ARGS__ },                                    \
            .args_offset = 1,                                                 \
            .u.native = _function,                                            \
            .object = { .type = NJS_FUNCTION,                                 \
                        .shared = 1,                                          \
                        .extensible = 1 },                                    \
        }                                                                     \
    }                                                                         \
}


#define njs_prop_handler(_handler) {                                          \
    .data = {                                                                 \
        .type = NJS_INVALID,                                                  \
        .truth = 1,                                                           \
        .u = { .prop_handler = _handler }                                     \
    }                                                                         \
}


#define njs_is_null(value)                                                    \
    ((value)->type == NJS_NULL)


#define njs_is_undefined(value)                                               \
    ((value)->type == NJS_UNDEFINED)


#define njs_is_null_or_undefined(value)                                       \
    ((value)->type <= NJS_UNDEFINED)


#define njs_is_boolean(value)                                                 \
    ((value)->type == NJS_BOOLEAN)


#define njs_is_null_or_undefined_or_boolean(value)                            \
    ((value)->type <= NJS_BOOLEAN)


#define njs_is_true(value)                                                    \
    ((value)->data.truth != 0)


#define njs_is_number(value)                                                  \
    ((value)->type == NJS_NUMBER)


/* Testing for NaN first generates a better code at least on i386/amd64. */

#define njs_is_number_true(num)                                               \
    (!isnan(num) && num != 0)


#define njs_is_numeric(value)                                                 \
    ((value)->type <= NJS_NUMBER)


#define njs_is_string(value)                                                  \
    ((value)->type == NJS_STRING)

#define njs_is_error(value)                                                   \
    ((value)->type >= NJS_OBJECT_ERROR                                        \
     && (value)->type <= NJS_OBJECT_URI_ERROR)


/*
 * The truth field coincides with short_string.size and short_string.length
 * so when string size and length are zero the string's value is false and
 * otherwise is true.
 */
#define njs_string_truth(value, size)


#define njs_string_get(value, str)                                            \
    do {                                                                      \
        if ((value)->short_string.size != NJS_STRING_LONG) {                  \
            (str)->length = (value)->short_string.size;                       \
            (str)->start = (u_char *) (value)->short_string.start;            \
                                                                              \
        } else {                                                              \
            (str)->length = (value)->long_string.size;                        \
            (str)->start = (u_char *) (value)->long_string.data->start;       \
        }                                                                     \
    } while (0)


#define njs_string_short_start(value)                                         \
    (value)->short_string.start


#define njs_string_short_set(value, _size, _length)                           \
    do {                                                                      \
        (value)->type = NJS_STRING;                                           \
        njs_string_truth(value, _size);                                       \
        (value)->short_string.size = _size;                                   \
        (value)->short_string.length = _length;                               \
    } while (0)


#define njs_string_length_set(value, _length)                                 \
    do {                                                                      \
        if ((value)->short_string.size != NJS_STRING_LONG) {                  \
            (value)->short_string.length = length;                            \
                                                                              \
        } else {                                                              \
            (value)->long_string.data->length = length;                       \
        }                                                                     \
    } while (0)

#define njs_is_primitive(value)                                               \
    ((value)->type <= NJS_STRING)


#define njs_is_data(value)                                                    \
    ((value)->type == NJS_DATA)


#define njs_is_object(value)                                                  \
    ((value)->type >= NJS_OBJECT)


#define njs_is_object_value(value)                                            \
    ((value)->type == NJS_OBJECT_VALUE)


#define njs_object_value_type(type)                                           \
    (type + NJS_OBJECT)


#define njs_is_array(value)                                                   \
    ((value)->type == NJS_ARRAY)


#define njs_is_function(value)                                                \
    ((value)->type == NJS_FUNCTION)


#define njs_is_regexp(value)                                                  \
    ((value)->type == NJS_REGEXP)


#define njs_is_date(value)                                                    \
    ((value)->type == NJS_DATE)


#define njs_is_external(value)                                                \
    ((value)->type == NJS_EXTERNAL)


#define njs_is_valid(value)                                                   \
    ((value)->type != NJS_INVALID)


#define njs_bool(value)                                                       \
    ((value)->data.truth)


#define njs_number(value)                                                     \
    ((value)->data.u.number)


#define njs_data(value)                                                       \
    ((value)->data.u.data)


#define njs_function(value)                                                   \
    ((value)->data.u.function)


#define njs_object(value)                                                     \
    ((value)->data.u.object)


#define njs_object_hash(value)                                                \
    (&(value)->data.u.object->hash)


#define njs_array(value)                                                      \
    ((value)->data.u.array)


#define njs_array_len(value)                                                  \
    ((value)->data.u.array->length)


#define njs_array_start(value)                                                \
    ((value)->data.u.array->start)


#define njs_date(value)                                                       \
    ((value)->data.u.date)


#define njs_object_value(_value)                                              \
    (&(_value)->data.u.object_value->value)


#define njs_set_undefined(value)                                              \
    *(value) = njs_value_undefined


#define njs_set_boolean(value, yn)                                            \
    *(value) = yn ? njs_value_true : njs_value_false


#define njs_set_true(value)                                                   \
    *(value) = njs_value_true


#define njs_set_false(value)                                                  \
    *(value) = njs_value_false


nxt_inline void
njs_set_number(njs_value_t *value, double num)
{
    value->data.u.number = num;
    value->type = NJS_NUMBER;
    value->data.truth = njs_is_number_true(num);
}


nxt_inline void
njs_set_data(njs_value_t *value, void *data)
{
    value->data.u.data = data;
    value->type = NJS_DATA;
    value->data.truth = 1;
}


nxt_inline void
njs_set_object(njs_value_t *value, njs_object_t *object)
{
    value->data.u.object = object;
    value->type = NJS_OBJECT;
    value->data.truth = 1;
}


nxt_inline void
njs_set_type_object(njs_value_t *value, njs_object_t *object,
    nxt_uint_t type)
{
    value->data.u.object = object;
    value->type = type;
    value->data.truth = 1;
}


nxt_inline void
njs_set_array(njs_value_t *value, njs_array_t *array)
{
    value->data.u.array = array;
    value->type = NJS_ARRAY;
    value->data.truth = 1;
}


nxt_inline void
njs_set_date(njs_value_t *value, njs_date_t *date)
{
    value->data.u.date = date;
    value->type = NJS_DATE;
    value->data.truth = 1;
}


nxt_inline void
njs_set_object_value(njs_value_t *value, njs_object_value_t *object_value)
{
    value->data.u.object_value = object_value;
    value->type = NJS_OBJECT_VALUE;
    value->data.truth = 1;
}


#define njs_set_invalid(value)                                                \
    (value)->type = NJS_INVALID


#if 0 /* GC: todo */

#define njs_retain(value)                                                     \
    do {                                                                      \
        if ((value)->data.truth == NJS_STRING_LONG) {                         \
            njs_value_retain(value);                                          \
        }                                                                     \
    } while (0)


#define njs_release(vm, value)                                                \
    do {                                                                      \
        if ((value)->data.truth == NJS_STRING_LONG) {                         \
            njs_value_release((vm), (value));                                 \
        }                                                                     \
    } while (0)

#else

#define njs_retain(value)
#define njs_release(vm, value)

#endif


void njs_value_retain(njs_value_t *value);
void njs_value_release(njs_vm_t *vm, njs_value_t *value);
nxt_bool_t njs_values_strict_equal(const njs_value_t *val1,
    const njs_value_t *val2);
njs_ret_t njs_value_to_primitive(njs_vm_t *vm, njs_value_t *value,
    nxt_uint_t hint);
njs_array_t *njs_value_enumerate(njs_vm_t *vm, const njs_value_t *value,
    njs_object_enum_t kind, nxt_bool_t all);
njs_array_t *njs_value_own_enumerate(njs_vm_t *vm, const njs_value_t *value,
    njs_object_enum_t kind, nxt_bool_t all);
const char *njs_type_string(njs_value_type_t type);
const char *njs_arg_type_string(uint8_t arg);


extern const njs_value_t  njs_value_null;
extern const njs_value_t  njs_value_undefined;
extern const njs_value_t  njs_value_false;
extern const njs_value_t  njs_value_true;
extern const njs_value_t  njs_value_zero;
extern const njs_value_t  njs_value_nan;
extern const njs_value_t  njs_value_invalid;

extern const njs_value_t  njs_string_empty;
extern const njs_value_t  njs_string_comma;
extern const njs_value_t  njs_string_null;
extern const njs_value_t  njs_string_undefined;
extern const njs_value_t  njs_string_boolean;
extern const njs_value_t  njs_string_false;
extern const njs_value_t  njs_string_true;
extern const njs_value_t  njs_string_number;
extern const njs_value_t  njs_string_minus_zero;
extern const njs_value_t  njs_string_minus_infinity;
extern const njs_value_t  njs_string_plus_infinity;
extern const njs_value_t  njs_string_nan;
extern const njs_value_t  njs_string_string;
extern const njs_value_t  njs_string_object;
extern const njs_value_t  njs_string_function;
extern const njs_value_t  njs_string_memory_error;


#endif /* _NJS_VALUE_H_INCLUDED_ */
