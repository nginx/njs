
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VALUE_H_INCLUDED_
#define _NJS_VALUE_H_INCLUDED_


/*
 * The order of the enum is used in njs_vmcode_typeof()
 * and njs_object_prototype_to_string().
 */

typedef enum {
    NJS_NULL,
    NJS_UNDEFINED,

    /* The order of the above type is used in njs_is_null_or_undefined(). */

    NJS_BOOLEAN,
    /*
     * The order of the above type is used in
     * njs_is_null_or_undefined_or_boolean().
     */
    NJS_NUMBER,
    /*
     * The order of the above type is used in njs_is_numeric().
     * Booleans, null and void values can be used in mathematical operations:
     *   a numeric value of the true value is one,
     *   a numeric value of the null and false values is zero,
     *   a numeric value of the void value is NaN.
     */
    NJS_SYMBOL,

    NJS_STRING,

    /* The order of the above type is used in njs_is_primitive(). */

    NJS_DATA,

    /*
     * The invalid value type is used:
     *   for uninitialized array members,
     *   to detect non-declared explicitly or implicitly variables,
     *   for native property getters.
     */
    NJS_INVALID,

    NJS_OBJECT                = 0x10,
    NJS_ARRAY,
#define NJS_OBJECT_SPECIAL_MIN  (NJS_FUNCTION)
    NJS_FUNCTION,
    NJS_REGEXP,
    NJS_DATE,
    NJS_TYPED_ARRAY,
#define NJS_OBJECT_SPECIAL_MAX  (NJS_TYPED_ARRAY + 1)
    NJS_PROMISE,
    NJS_OBJECT_VALUE,
    NJS_ARRAY_BUFFER,
    NJS_DATA_VIEW,
    NJS_VALUE_TYPE_MAX
} njs_value_type_t;


typedef enum {
    NJS_DATA_TAG_ANY = 0,
    NJS_DATA_TAG_EXTERNAL,
    NJS_DATA_TAG_TEXT_ENCODER,
    NJS_DATA_TAG_TEXT_DECODER,
    NJS_DATA_TAG_ARRAY_ITERATOR,
    NJS_DATA_TAG_FOREACH_NEXT,
    NJS_DATA_TAG_MAX
} njs_data_tag_t;


typedef struct njs_string_s           njs_string_t;
typedef struct njs_object_s           njs_object_t;
typedef struct njs_object_value_s     njs_object_value_t;
typedef struct njs_function_lambda_s  njs_function_lambda_t;
typedef struct njs_regexp_pattern_s   njs_regexp_pattern_t;
typedef struct njs_array_s            njs_array_t;
typedef struct njs_array_buffer_s     njs_array_buffer_t;
typedef struct njs_typed_array_s      njs_typed_array_t;
typedef struct njs_typed_array_s      njs_data_view_t;
typedef struct njs_regexp_s           njs_regexp_t;
typedef struct njs_date_s             njs_date_t;
typedef struct njs_object_value_s     njs_promise_t;
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

        uint16_t                      magic16;
        uint32_t                      magic32;

        union {
            double                    number;
            njs_object_t              *object;
            njs_array_t               *array;
            njs_array_buffer_t        *array_buffer;
            njs_typed_array_t         *typed_array;
            njs_data_view_t           *data_view;
            njs_object_value_t        *object_value;
            njs_function_t            *function;
            njs_function_lambda_t     *lambda;
            njs_regexp_t              *regexp;
            njs_date_t                *date;
            njs_promise_t             *promise;
            njs_prop_handler_t        prop_handler;
            njs_value_t               *value;
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

    njs_value_type_t                  type:8;  /* 6 bits */
};


typedef struct {
    /* Get, also Set if writable, also Delete if configurable. */
    njs_prop_handler_t  prop_handler;
    uint32_t            magic32;
    unsigned            writable:1;
    unsigned            configurable:1;
    unsigned            enumerable:1;

    njs_exotic_keys_t   keys;

    /* A shared hash of njs_object_prop_t for externals. */
    njs_lvlhsh_t        external_shared_hash;
} njs_exotic_slots_t;


struct njs_object_s {
    /* A private hash of njs_object_prop_t. */
    njs_lvlhsh_t                      hash;

    /* A shared hash of njs_object_prop_t. */
    njs_lvlhsh_t                      shared_hash;

    njs_object_t                      *__proto__;
    njs_exotic_slots_t                *slots;

    /* The type is used in constructor prototypes. */
    njs_value_type_t                  type:8;
    uint8_t                           shared;     /* 1 bit */

    uint8_t                           extensible:1;
    uint8_t                           error_data:1;
    uint8_t                           stack_attached:1;
    uint8_t                           fast_array:1;
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


struct njs_array_buffer_s {
    njs_object_t                      object;
    size_t                            size;
    union {
        uint8_t                       *u8;
        uint16_t                      *u16;
        uint32_t                      *u32;
        uint64_t                      *u64;
        int8_t                        *i8;
        int16_t                       *i16;
        int32_t                       *i32;
        int64_t                       *i64;
        float                         *f32;
        double                        *f64;

        void                          *data;
    } u;
};


struct njs_typed_array_s {
    njs_object_t                      object;
    njs_array_buffer_t                *buffer;
    size_t                            offset; // byte_offset / element_size
    size_t                            byte_length;
    uint8_t                           type;
};


struct njs_function_s {
    njs_object_t                      object;

    /* Number of bound args excluding 'this'. */
    uint8_t                           bound_args;

    uint8_t                           args_count:4;

    uint8_t                           closure_copied:1;
    uint8_t                           native:1;
    uint8_t                           ctor:1;
    uint8_t                           global_this:1;
    uint8_t                           global:1;

    uint8_t                           magic8;

    union {
        njs_function_lambda_t         *lambda;
        njs_function_native_t         native;
    } u;

    void                              *context;

    /* Bound args including 'this'. */
    njs_value_t                       *bound;
};


struct njs_regexp_s {
    njs_object_t                      object;
    njs_value_t                       last_index;
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
    njs_promise_t                     promise;
} njs_object_prototype_t;


typedef struct {
    njs_function_t            constructor;
    const njs_object_init_t   *constructor_props;
    const njs_object_init_t   *prototype_props;
    njs_object_prototype_t    prototype_value;
} njs_object_type_init_t;


typedef enum {
    NJS_ENUM_KEYS,
    NJS_ENUM_VALUES,
    NJS_ENUM_BOTH,
} njs_object_enum_t;


typedef enum {
    NJS_ENUM_STRING = 1,
    NJS_ENUM_SYMBOL = 2,
} njs_object_enum_type_t;


typedef enum {
    NJS_PROPERTY = 0,
    NJS_ACCESSOR,
    NJS_PROPERTY_REF,
    NJS_PROPERTY_PLACE_REF,
    NJS_PROPERTY_TYPED_ARRAY_REF,
    NJS_PROPERTY_HANDLER,
    NJS_WHITEOUT,
} njs_object_prop_type_t;


typedef enum {
    NJS_PROPERTY_QUERY_GET = 0,
    NJS_PROPERTY_QUERY_SET,
    NJS_PROPERTY_QUERY_DELETE,
} njs_prop_query_t;


/*
 * Attributes are generally used as Boolean values.
 * The UNSET value is can be seen:
 * for newly created property descriptors in njs_define_property(),
 * for writable attribute of accessor descriptors (desc->writable
 * cannot be used as a boolean value).
 */
typedef enum {
    NJS_ATTRIBUTE_FALSE = 0,
    NJS_ATTRIBUTE_TRUE = 1,
    NJS_ATTRIBUTE_UNSET,
} njs_object_attribute_t;


struct njs_object_prop_s {
    njs_value_t                 name;

    union {
        njs_value_t             value;
        struct {
            njs_function_t      *getter;
            njs_function_t      *setter;
        } accessor;
    } u;

#define njs_prop_value(_p)      (&(_p)->u.value)
#define njs_prop_handler(_p)    (_p)->u.value.data.u.prop_handler
#define njs_prop_ref(_p)        (_p)->u.value.data.u.value
#define njs_prop_typed_ref(_p)  (_p)->u.value.data.u.typed_array
#define njs_prop_magic16(_p)    (_p)->u.value.data.magic16
#define njs_prop_magic32(_p)    (_p)->u.value.data.magic32
#define NJS_PROP_PTR_UNSET      ((void *) (uintptr_t) -1)
#define njs_prop_getter(_p)     (_p)->u.accessor.getter
#define njs_prop_setter(_p)     (_p)->u.accessor.setter

    njs_object_prop_type_t      type:8;          /* 3 bits */

    njs_object_attribute_t      writable:8;      /* 2 bits */
    njs_object_attribute_t      enumerable:8;    /* 2 bits */
    njs_object_attribute_t      configurable:8;  /* 2 bits */
};


typedef struct {
    njs_lvlhsh_query_t          lhq;

    uint8_t                     query;

    /* scratch is used to get the value of an NJS_PROPERTY_HANDLER property. */
    njs_object_prop_t           scratch;

    njs_value_t                 key;

    njs_object_prop_t           *own_whiteout;
    uint8_t                     temp;
    uint8_t                     own;
} njs_property_query_t;


#define njs_value(_type, _truth, _number) {                                   \
    .data = {                                                                 \
        .type = _type,                                                        \
        .truth = _truth,                                                      \
        .u.number = _number,                                                  \
    }                                                                         \
}


#define njs_wellknown_symbol(key) {                                           \
    .data = {                                                                 \
        .type = NJS_SYMBOL,                                                   \
        .truth = 1,                                                           \
        .magic32 = key,                                                       \
        .u = { .value = NULL }                                                \
    }                                                                         \
}


#define njs_string(s) {                                                       \
    .short_string = {                                                         \
        .type = NJS_STRING,                                                   \
        .size = njs_length(s),                                                \
        .length = njs_length(s),                                              \
        .start = s,                                                           \
    }                                                                         \
}


/* NJS_STRING_LONG is set for both big and little endian platforms. */

#define njs_long_string(s) {                                                  \
    .long_string = {                                                          \
        .type = NJS_STRING,                                                   \
        .truth = (NJS_STRING_LONG << 4) | NJS_STRING_LONG,                    \
        .size = njs_length(s),                                                \
        .data = & (njs_string_t) {                                            \
            .start = (u_char *) s,                                            \
            .length = njs_length(s),                                          \
        }                                                                     \
    }                                                                         \
}


#define _njs_function(_function, _args_count, _ctor, _magic) {                \
    .native = 1,                                                              \
    .magic8 = _magic,                                                         \
    .args_count = _args_count,                                                \
    .ctor = _ctor,                                                            \
    .u.native = _function,                                                    \
    .object = { .type = NJS_FUNCTION,                                         \
                .shared = 1,                                                  \
                .extensible = 1 },                                            \
}


#define _njs_native_function(_func, _args, _ctor, _magic) {                   \
    .data = {                                                                 \
        .type = NJS_FUNCTION,                                                 \
        .truth = 1,                                                           \
        .u.function = & (njs_function_t) _njs_function(_func, _args,          \
                                                       _ctor, _magic)         \
    }                                                                         \
}


#define njs_native_function(_function, _args_count)                           \
    _njs_native_function(_function, _args_count, 0, 0)


#define njs_native_function2(_function, _args_count, _magic)                  \
    _njs_native_function(_function, _args_count, 0, _magic)


#define njs_getter(_function, _magic)                                         \
    {                                                                         \
        .getter = & (njs_function_t) _njs_function(_function, 0, 0, _magic),  \
        .setter = NULL,                                                       \
    }


#define njs_accessor(_getter, _m1, _setter, _m2)                              \
    {                                                                         \
        .getter = & (njs_function_t) _njs_function(_getter, 0, 0, _m1),       \
        .setter = & (njs_function_t) _njs_function(_setter, 0, 0, _m2),       \
    }


#define njs_native_ctor(_function, _args_count, _magic)                       \
    _njs_function(_function, _args_count, 1, _magic)


#define njs_prop_handler2(_handler, _magic16, _magic32) {                     \
    .data = {                                                                 \
        .type = NJS_INVALID,                                                  \
        .truth = 1,                                                           \
        .magic16 = _magic16,                                                  \
        .magic32 = _magic32,                                                  \
        .u = { .prop_handler = _handler }                                     \
    }                                                                         \
}


#define njs_is_null(value)                                                    \
    ((value)->type == NJS_NULL)


#define njs_is_undefined(value)                                               \
    ((value)->type == NJS_UNDEFINED)


#define njs_is_defined(value)                                                 \
    ((value)->type != NJS_UNDEFINED)


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


#define njs_is_symbol(value)                                                  \
    ((value)->type == NJS_SYMBOL)


#define njs_is_string(value)                                                  \
    ((value)->type == NJS_STRING)


#define njs_is_key(value)                                                     \
    (njs_is_string(value) || njs_is_symbol(value))


#define njs_is_index_or_key(value)                                            \
    (njs_is_number(value) || njs_is_key(value))


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


#define njs_make_tag(proto_id)                                                \
    (((njs_uint_t) proto_id << 8) | NJS_DATA_TAG_EXTERNAL)


#define njs_is_data(value, tag)                                               \
    ((value)->type == NJS_DATA                                                \
     && ((tag) == njs_make_tag(NJS_PROTO_ID_ANY)                              \
         || value->data.magic32 == (tag)))


#define njs_is_object(value)                                                  \
    ((value)->type >= NJS_OBJECT)


#define njs_has_prototype(vm, value, proto)                                   \
    (((njs_object_prototype_t *)                                              \
        njs_object(value)->__proto__ - (vm)->prototypes) == proto)


#define njs_is_object_value(value)                                            \
    ((value)->type == NJS_OBJECT_VALUE)


#define njs_is_object_boolean(_value)                                         \
    (((_value)->type == NJS_OBJECT_VALUE)                                     \
     && njs_is_boolean(njs_object_value(_value)))


#define njs_is_object_number(_value)                                          \
    (((_value)->type == NJS_OBJECT_VALUE)                                     \
     && njs_is_number(njs_object_value(_value)))


#define njs_is_object_symbol(_value)                                          \
    (((_value)->type == NJS_OBJECT_VALUE)                                     \
     && njs_is_symbol(njs_object_value(_value)))


#define njs_is_object_string(_value)                                          \
    (((_value)->type == NJS_OBJECT_VALUE)                                     \
     && njs_is_string(njs_object_value(_value)))


#define njs_is_object_primitive(_value)                                       \
    (((_value)->type == NJS_OBJECT_VALUE)                                     \
     && njs_is_primitive(njs_object_value(_value)))


#define njs_is_object_data(_value, tag)                                       \
    (((_value)->type == NJS_OBJECT_VALUE)                                     \
     && njs_is_data(njs_object_value(_value), tag))


#define njs_is_array(value)                                                   \
    ((value)->type == NJS_ARRAY)


#define njs_is_fast_array(value)                                              \
    (njs_is_array(value) && njs_array(value)->object.fast_array)


#define njs_is_array_buffer(value)                                            \
    ((value)->type == NJS_ARRAY_BUFFER)


#define njs_is_typed_array(value)                                             \
    ((value)->type == NJS_TYPED_ARRAY)


#define njs_is_detached_buffer(buffer)                                        \
    ((buffer)->u.data == NULL)


#define njs_is_data_view(value)                                               \
    ((value)->type == NJS_DATA_VIEW)


#define njs_is_typed_array_uint8(value)                                       \
    (njs_is_typed_array(value)                                                \
     && njs_typed_array(value)->type == NJS_OBJ_TYPE_UINT8_ARRAY)


#define njs_is_function(value)                                                \
    ((value)->type == NJS_FUNCTION)


#define njs_is_function_or_undefined(value)                                   \
    ((value)->type == NJS_FUNCTION || (value)->type == NJS_UNDEFINED)


#define njs_is_constructor(value)                                             \
    (njs_is_function(value) && njs_function(value)->ctor)


#define njs_is_regexp(value)                                                  \
    ((value)->type == NJS_REGEXP)


#define njs_is_date(value)                                                    \
    ((value)->type == NJS_DATE)


#define njs_is_promise(value)                                                 \
    ((value)->type == NJS_PROMISE)


#define njs_is_error(value)                                                   \
    ((value)->type == NJS_OBJECT && njs_object(value)->error_data)


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


#define njs_function_lambda(value)                                            \
    ((value)->data.u.function->u.lambda)


#define njs_object(value)                                                     \
    ((value)->data.u.object)


#define njs_object_hash(value)                                                \
    (&(value)->data.u.object->hash)


#define njs_object_slots(value)                                               \
    ((value)->data.u.object->slots)


#define njs_array(value)                                                      \
    ((value)->data.u.array)


#define njs_array_len(value)                                                  \
    ((value)->data.u.array->length)


#define njs_array_buffer(value)                                               \
    ((value)->data.u.array_buffer)


#define njs_data_view(value)                                                  \
    ((value)->data.u.data_view)


#define njs_typed_array(value)                                                \
    ((value)->data.u.typed_array)


#define njs_typed_array_buffer(value)                                         \
    ((value)->buffer)


#define njs_array_start(value)                                                \
    ((value)->data.u.array->start)


#define njs_date(value)                                                       \
    ((value)->data.u.date)


#define njs_promise(value)                                                    \
    ((value)->data.u.promise)


#define njs_regexp(value)                                                     \
    ((value)->data.u.regexp)


#define njs_regexp_pattern(value)                                             \
    ((value)->data.u.regexp->pattern)


#define njs_object_value(_value)                                              \
    (&(_value)->data.u.object_value->value)


#define njs_object_data(_value)                                               \
    njs_data(njs_object_value(_value))


#define njs_set_undefined(value)                                              \
    *(value) = njs_value_undefined


#define njs_set_null(value)                                                   \
    *(value) = njs_value_null


#define njs_set_true(value)                                                   \
    *(value) = njs_value_true


#define njs_set_false(value)                                                  \
    *(value) = njs_value_false


#define njs_symbol_key(value)                                                 \
    ((value)->data.magic32)


#define njs_symbol_eq(value1, value2)                                         \
    (njs_symbol_key(value1) == njs_symbol_key(value2))


extern const njs_value_t  njs_value_null;
extern const njs_value_t  njs_value_undefined;
extern const njs_value_t  njs_value_false;
extern const njs_value_t  njs_value_true;
extern const njs_value_t  njs_value_zero;
extern const njs_value_t  njs_value_nan;
extern const njs_value_t  njs_value_invalid;

extern const njs_value_t  njs_string_empty;
extern const njs_value_t  njs_string_empty_regexp;
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
extern const njs_value_t  njs_string_symbol;
extern const njs_value_t  njs_string_string;
extern const njs_value_t  njs_string_data;
extern const njs_value_t  njs_string_type;
extern const njs_value_t  njs_string_name;
extern const njs_value_t  njs_string_external;
extern const njs_value_t  njs_string_invalid;
extern const njs_value_t  njs_string_object;
extern const njs_value_t  njs_string_function;
extern const njs_value_t  njs_string_anonymous;
extern const njs_value_t  njs_string_memory_error;


njs_inline void
njs_set_boolean(njs_value_t *value, unsigned yn)
{
    const njs_value_t  *retval;

    /* Using const retval generates a better code at least on i386/amd64. */
    retval = (yn) ? &njs_value_true : &njs_value_false;

    *value = *retval;
}


njs_inline void
njs_set_number(njs_value_t *value, double num)
{
    value->data.u.number = num;
    value->type = NJS_NUMBER;
    value->data.truth = njs_is_number_true(num);
}


njs_inline void
njs_set_int32(njs_value_t *value, int32_t num)
{
    value->data.u.number = num;
    value->type = NJS_NUMBER;
    value->data.truth = (num != 0);
}


njs_inline void
njs_set_uint32(njs_value_t *value, uint32_t num)
{
    value->data.u.number = num;
    value->type = NJS_NUMBER;
    value->data.truth = (num != 0);
}


njs_inline void
njs_set_symbol(njs_value_t *value, uint32_t symbol, njs_value_t *name)
{
    value->data.magic32 = symbol;
    value->type = NJS_SYMBOL;
    value->data.truth = 1;
    value->data.u.value = name;
}


njs_inline void
njs_set_data(njs_value_t *value, void *data, njs_data_tag_t tag)
{
    value->data.magic32 = tag;
    value->data.u.data = data;
    value->type = NJS_DATA;
    value->data.truth = 1;
}


njs_inline void
njs_set_object(njs_value_t *value, njs_object_t *object)
{
    value->data.u.object = object;
    value->type = NJS_OBJECT;
    value->data.truth = 1;
}


njs_inline void
njs_set_type_object(njs_value_t *value, njs_object_t *object,
    njs_uint_t type)
{
    value->data.u.object = object;
    value->type = type;
    value->data.truth = 1;
}


njs_inline void
njs_set_array(njs_value_t *value, njs_array_t *array)
{
    value->data.u.array = array;
    value->type = NJS_ARRAY;
    value->data.truth = 1;
}


njs_inline void
njs_set_array_buffer(njs_value_t *value, njs_array_buffer_t *array)
{
    value->data.u.array_buffer = array;
    value->type = NJS_ARRAY_BUFFER;
    value->data.truth = 1;
}


njs_inline void
njs_set_typed_array(njs_value_t *value, njs_typed_array_t *array)
{
    value->data.u.typed_array = array;
    value->type = NJS_TYPED_ARRAY;
    value->data.truth = 1;
}


njs_inline void
njs_set_data_view(njs_value_t *value, njs_data_view_t *array)
{
    value->data.u.data_view = array;
    value->type = NJS_DATA_VIEW;
    value->data.truth = 1;
}


njs_inline void
njs_set_function(njs_value_t *value, njs_function_t *function)
{
    value->data.u.function = function;
    value->type = NJS_FUNCTION;
    value->data.truth = 1;
}


njs_inline void
njs_set_date(njs_value_t *value, njs_date_t *date)
{
    value->data.u.date = date;
    value->type = NJS_DATE;
    value->data.truth = 1;
}


njs_inline void
njs_set_promise(njs_value_t *value, njs_promise_t *promise)
{
    value->data.u.promise = promise;
    value->type = NJS_PROMISE;
    value->data.truth = 1;
}


njs_inline void
njs_set_regexp(njs_value_t *value, njs_regexp_t *regexp)
{
    value->data.u.regexp = regexp;
    value->type = NJS_REGEXP;
    value->data.truth = 1;
}


njs_inline void
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
njs_int_t njs_value_to_primitive(njs_vm_t *vm, njs_value_t *dst,
    njs_value_t *value, njs_uint_t hint);
njs_array_t *njs_value_enumerate(njs_vm_t *vm, njs_value_t *value,
    njs_object_enum_t kind, njs_object_enum_type_t type, njs_bool_t all);
njs_array_t *njs_value_own_enumerate(njs_vm_t *vm, njs_value_t *value,
    njs_object_enum_t kind, njs_object_enum_type_t type, njs_bool_t all);
njs_int_t njs_value_of(njs_vm_t *vm, njs_value_t *value, njs_value_t *retval);
njs_int_t njs_value_length(njs_vm_t *vm, njs_value_t *value, int64_t *dst);
const char *njs_type_string(njs_value_type_t type);

njs_int_t njs_primitive_value_to_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *src);
njs_int_t njs_primitive_value_to_chain(njs_vm_t *vm, njs_chb_t *chain,
    const njs_value_t *src);
double njs_string_to_number(const njs_value_t *value, njs_bool_t parse_float);
njs_int_t njs_int64_to_string(njs_vm_t *vm, njs_value_t *value, int64_t i64);

njs_bool_t njs_string_eq(const njs_value_t *v1, const njs_value_t *v2);

njs_int_t njs_property_query(njs_vm_t *vm, njs_property_query_t *pq,
    njs_value_t *value, njs_value_t *key);

njs_int_t njs_value_property(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *key, njs_value_t *retval);
njs_int_t njs_value_property_set(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *key, njs_value_t *setval);
njs_int_t njs_value_property_delete(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *key, njs_value_t *removed, njs_bool_t thrw);
njs_int_t njs_value_to_object(njs_vm_t *vm, njs_value_t *value);

void njs_symbol_conversion_failed(njs_vm_t *vm, njs_bool_t to_string);

njs_int_t njs_value_species_constructor(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *default_constructor, njs_value_t *dst);

njs_int_t njs_value_method(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *retval);


njs_inline void
njs_property_query_init(njs_property_query_t *pq, njs_prop_query_t query,
    uint32_t hash, uint8_t own)
{
        pq->query = query;
        pq->lhq.key_hash = hash;
        pq->own = own;

        if (query == NJS_PROPERTY_QUERY_SET) {
            pq->lhq.value = NULL;
            pq->own_whiteout = NULL;
            pq->temp = 0;
        }
}


njs_inline njs_int_t
njs_value_property_i64(njs_vm_t *vm, njs_value_t *value, int64_t index,
    njs_value_t *retval)
{
    njs_value_t  key;

    njs_set_number(&key, index);

    return njs_value_property(vm, value, &key, retval);
}


njs_inline njs_int_t
njs_value_property_i64_set(njs_vm_t *vm, njs_value_t *value, int64_t index,
    njs_value_t *setval)
{
    njs_value_t  key;

    njs_set_number(&key, index);

    return njs_value_property_set(vm, value, &key, setval);
}


njs_inline njs_int_t
njs_value_property_i64_delete(njs_vm_t *vm, njs_value_t *value, int64_t index,
    njs_value_t *removed)
{
    njs_value_t  key;

    njs_set_number(&key, index);

    return njs_value_property_delete(vm, value, &key, removed, 1);
}


njs_inline njs_bool_t
njs_values_same_non_numeric(const njs_value_t *val1, const njs_value_t *val2)
{
    if (njs_is_string(val1)) {
        return njs_string_eq(val1, val2);
    }

    if (njs_is_symbol(val1)) {
        return njs_symbol_eq(val1, val2);
    }

    return (njs_object(val1) == njs_object(val2));
}


njs_inline njs_bool_t
njs_values_strict_equal(const njs_value_t *val1, const njs_value_t *val2)
{
    if (val1->type != val2->type) {
        return 0;
    }

    if (njs_is_numeric(val1)) {

        if (njs_is_undefined(val1)) {
            return 1;
        }

        /* Infinities are handled correctly by comparision. */
        return (njs_number(val1) == njs_number(val2));
    }

    return njs_values_same_non_numeric(val1, val2);
}


njs_inline njs_bool_t
njs_values_same(const njs_value_t *val1, const njs_value_t *val2)
{
    double  num1, num2;

    if (val1->type != val2->type) {
        return 0;
    }

    if (njs_is_numeric(val1)) {

        if (njs_is_undefined(val1)) {
            return 1;
        }

        num1 = njs_number(val1);
        num2 = njs_number(val2);

        if (njs_slow_path(isnan(num1) && isnan(num2))) {
            return 1;
        }

        if (njs_slow_path(num1 == 0 && num2 == 0
                          && (signbit(num1) ^ signbit(num2))))
        {
            return 0;
        }

        /* Infinities are handled correctly by comparision. */
        return num1 == num2;
    }

    return njs_values_same_non_numeric(val1, val2);
}


njs_inline njs_bool_t
njs_values_same_zero(const njs_value_t *val1, const njs_value_t *val2)
{
    double  num1, num2;

    if (val1->type != val2->type) {
        return 0;
    }

    if (njs_is_numeric(val1)) {

        if (njs_is_undefined(val1)) {
            return 1;
        }

        num1 = njs_number(val1);
        num2 = njs_number(val2);

        if (njs_slow_path(isnan(num1) && isnan(num2))) {
            return 1;
        }

        /* Infinities are handled correctly by comparision. */
        return num1 == num2;
    }

    return njs_values_same_non_numeric(val1, val2);
}


#endif /* _NJS_VALUE_H_INCLUDED_ */
