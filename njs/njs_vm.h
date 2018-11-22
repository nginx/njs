
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VM_H_INCLUDED_
#define _NJS_VM_H_INCLUDED_


#include <nxt_trace.h>
#include <nxt_queue.h>
#include <nxt_regex.h>
#include <nxt_random.h>
#include <nxt_djb_hash.h>
#include <nxt_mem_cache_pool.h>


#define NJS_MAX_STACK_SIZE       (16 * 1024 * 1024)

/*
 * Negative return values handled by nJSVM interpreter as special events.
 * The values must be in range from -1 to -11, because -12 is minimal jump
 * offset on 32-bit platforms.
 *    -1 (NJS_ERROR/NXT_ERROR):  error or exception;
 *    -2 (NJS_AGAIN/NXT_AGAIN):  postpone nJSVM execution;
 *    -3:                        not used;
 *    -4 (NJS_STOP/NXT_DONE):    njs_vmcode_stop() has stopped execution,
 *                               execution has completed successfully;
 *    -5 (NJS_TRAP)              trap to convert objects to primitive values;
 *    -6 .. -11:                 not used.
 */

#define NJS_STOP                 NXT_DONE
#define NJS_TRAP                 (-5)

/* The last return value which preempts execution. */
#define NJS_PREEMPT              (-11)

/*  Traps events. */
typedef enum {
    NJS_TRAP_NUMBER = 0,
    NJS_TRAP_NUMBERS,
    NJS_TRAP_ADDITION,
    NJS_TRAP_COMPARISON,
    NJS_TRAP_INCDEC,
    NJS_TRAP_PROPERTY,
    NJS_TRAP_NUMBER_ARG,
    NJS_TRAP_STRING_ARG,
} njs_trap_t;


#define njs_trap(vm, code)                                                    \
    vm->trap = code, NJS_TRAP;


/*
 * A user-defined function is prepared to run.  This code is never
 * returned to interpreter, so the value can be shared with NJS_STOP.
 */
#define NJS_APPLIED              NXT_DONE


/*
 * NJS_PROPERTY_QUERY_GET must be less to NJS_PROPERTY_QUERY_SET
 * and NJS_PROPERTY_QUERY_DELETE.
 */
#define NJS_PROPERTY_QUERY_GET     0
#define NJS_PROPERTY_QUERY_SET     1
#define NJS_PROPERTY_QUERY_DELETE  2


/*
 * The order of the enum is used in njs_vmcode_typeof()
 * and njs_object_prototype_to_string().
 */

typedef enum {
    NJS_NULL                  = 0x00,
    NJS_VOID                  = 0x01,

    /* The order of the above type is used in njs_is_null_or_void(). */

    NJS_BOOLEAN               = 0x02,
    /*
     * The order of the above type is used in njs_is_null_or_void_or_boolean().
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


typedef struct njs_parser_s           njs_parser_t;

/*
 * njs_prop_handler_t operates as a property getter and/or setter.
 * The handler receives NULL setval if it is invoked in GET context and
 * non-null otherwise.
 */
typedef njs_ret_t (*njs_prop_handler_t) (njs_vm_t *vm, njs_value_t *value,
    njs_value_t *setval, njs_value_t *retval);
typedef njs_ret_t (*njs_function_native_t) (njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t retval);


typedef struct njs_string_s           njs_string_t;
typedef struct njs_object_s           njs_object_t;
typedef struct njs_object_init_s      njs_object_init_t;
typedef struct njs_object_value_s     njs_object_value_t;
typedef struct njs_array_s            njs_array_t;
typedef struct njs_function_lambda_s  njs_function_lambda_t;
typedef struct njs_regexp_s           njs_regexp_t;
typedef struct njs_regexp_pattern_s   njs_regexp_pattern_t;
typedef struct njs_date_s             njs_date_t;
typedef struct njs_frame_s            njs_frame_t;
typedef struct njs_native_frame_s     njs_native_frame_t;
typedef struct njs_property_next_s    njs_property_next_t;
typedef struct njs_parser_scope_s     njs_parser_scope_t;


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


typedef struct {
    nxt_str_t                       name;
    uint32_t                        line;
} njs_backtrace_entry_t;


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


typedef njs_ret_t (*njs_vmcode_operation_t)(njs_vm_t *vm, njs_value_t *value1,
    njs_value_t *value2);


#define njs_is_null(value)                                                    \
    ((value)->type == NJS_NULL)


#define njs_is_void(value)                                                    \
    ((value)->type == NJS_VOID)


#define njs_is_null_or_void(value)                                            \
    ((value)->type <= NJS_VOID)


#define njs_is_boolean(value)                                                 \
    ((value)->type == NJS_BOOLEAN)


#define njs_is_null_or_void_or_boolean(value)                                 \
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


#define njs_set_invalid(value)                                                \
    (value)->type = NJS_INVALID


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


#define NJS_VMCODE_3OPERANDS   0
#define NJS_VMCODE_2OPERANDS   1
#define NJS_VMCODE_1OPERAND    2
#define NJS_VMCODE_NO_OPERAND  3

#define NJS_VMCODE_NO_RETVAL   0
#define NJS_VMCODE_RETVAL      1


typedef struct {
    njs_vmcode_operation_t     operation;
    uint8_t                    operands;   /* 2 bits */
    uint8_t                    retval;     /* 1 bit  */
    uint8_t                    ctor;       /* 1 bit  */
} njs_vmcode_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                operand1;
    njs_index_t                operand2;
    njs_index_t                operand3;
} njs_vmcode_generic_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                index;
} njs_vmcode_1addr_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                dst;
    njs_index_t                src;
} njs_vmcode_2addr_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                dst;
    njs_index_t                src1;
    njs_index_t                src2;
} njs_vmcode_3addr_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                dst;
    njs_index_t                src;
} njs_vmcode_move_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_object_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_arguments_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    uintptr_t                  length;
} njs_vmcode_array_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_function_lambda_t      *lambda;
} njs_vmcode_function_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_regexp_pattern_t       *pattern;
} njs_vmcode_regexp_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_index_t                object;
} njs_vmcode_object_copy_t;


typedef struct {
    njs_vmcode_t               code;
    njs_ret_t                  offset;
} njs_vmcode_jump_t;


typedef struct {
    njs_vmcode_t               code;
    njs_ret_t                  offset;
    njs_index_t                cond;
} njs_vmcode_cond_jump_t;


typedef struct {
    njs_vmcode_t               code;
    njs_ret_t                  offset;
    njs_index_t                value1;
    njs_index_t                value2;
} njs_vmcode_equal_jump_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_index_t                value;
    njs_ret_t                  offset;
} njs_vmcode_test_jump_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                value;
    njs_index_t                object;
    njs_index_t                property;
} njs_vmcode_prop_get_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                value;
    njs_index_t                object;
    njs_index_t                property;
} njs_vmcode_prop_set_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                next;
    njs_index_t                object;
    njs_ret_t                  offset;
} njs_vmcode_prop_foreach_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_index_t                object;
    njs_index_t                next;
    njs_ret_t                  offset;
} njs_vmcode_prop_next_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                value;
    njs_index_t                constructor;
    njs_index_t                object;
} njs_vmcode_instance_of_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                nargs;
    njs_index_t                name;
} njs_vmcode_function_frame_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                nargs;
    njs_index_t                object;
    njs_index_t                method;
} njs_vmcode_method_frame_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_function_call_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_return_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_stop_t;


typedef struct {
    njs_vmcode_t               code;
    njs_ret_t                  offset;
    njs_index_t                value;
} njs_vmcode_try_start_t;


typedef struct {
    njs_vmcode_t               code;
    njs_ret_t                  offset;
    njs_index_t                exception;
} njs_vmcode_catch_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_throw_t;


typedef struct {
    njs_vmcode_t               code;
    njs_ret_t                  offset;
} njs_vmcode_try_end_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_finally_t;


typedef enum {
    NJS_SCOPE_ABSOLUTE = 0,
    NJS_SCOPE_GLOBAL = 1,
    NJS_SCOPE_CALLEE_ARGUMENTS = 2,
    /*
     * The argument and local VM scopes should separate because a
     * function may be called with any number of arguments.
     */
    NJS_SCOPE_ARGUMENTS = 3,
    NJS_SCOPE_LOCAL = 4,
    NJS_SCOPE_FUNCTION = NJS_SCOPE_LOCAL,

    NJS_SCOPE_CLOSURE = 5,
    /*
     * The block and shim scopes are not really VM scopes.
     * They used only on parsing phase.
     */
    NJS_SCOPE_BLOCK = 16,
    NJS_SCOPE_SHIM = 17,
} njs_scope_t;


/*
 * The maximum possible function nesting level is (16 - NJS_SCOPE_CLOSURE),
 * that is 11.  The 5 is reasonable limit.
 */
#define NJS_MAX_NESTING        5

#define NJS_SCOPES             (NJS_SCOPE_CLOSURE + NJS_MAX_NESTING)

#define NJS_SCOPE_SHIFT        4
#define NJS_SCOPE_MASK         ((uintptr_t) ((1 << NJS_SCOPE_SHIFT) - 1))

#define NJS_INDEX_CACHE        NJS_SCOPE_GLOBAL

#define NJS_INDEX_NONE         ((njs_index_t) 0)
#define NJS_INDEX_ERROR        ((njs_index_t) -1)
#define NJS_INDEX_THIS         ((njs_index_t) (0 | NJS_SCOPE_ARGUMENTS))

#define njs_scope_type(index)                                                 \
     ((uintptr_t) (index) & NJS_SCOPE_MASK)

#define njs_is_callee_argument_index(index)                                   \
    (((index) & NJS_SCOPE_CALLEE_ARGUMENTS) == NJS_SCOPE_CALLEE_ARGUMENTS)

enum njs_prototypes_e {
    NJS_PROTOTYPE_OBJECT = 0,
    NJS_PROTOTYPE_ARRAY,
    NJS_PROTOTYPE_BOOLEAN,
    NJS_PROTOTYPE_NUMBER,
    NJS_PROTOTYPE_STRING,
    NJS_PROTOTYPE_FUNCTION,
    NJS_PROTOTYPE_REGEXP,
    NJS_PROTOTYPE_DATE,
    NJS_PROTOTYPE_CRYPTO_HASH,
    NJS_PROTOTYPE_CRYPTO_HMAC,
    NJS_PROTOTYPE_ERROR,
    NJS_PROTOTYPE_EVAL_ERROR,
    NJS_PROTOTYPE_INTERNAL_ERROR,
    NJS_PROTOTYPE_RANGE_ERROR,
    NJS_PROTOTYPE_REF_ERROR,
    NJS_PROTOTYPE_SYNTAX_ERROR,
    NJS_PROTOTYPE_TYPE_ERROR,
    NJS_PROTOTYPE_URI_ERROR,
#define NJS_PROTOTYPE_MAX      (NJS_PROTOTYPE_URI_ERROR + 1)
};


#define njs_primitive_prototype_index(type)                                   \
    (NJS_PROTOTYPE_BOOLEAN + ((type) - NJS_BOOLEAN))


#define njs_error_prototype_index(type)                                       \
    (NJS_PROTOTYPE_ERROR + ((type) - NJS_OBJECT_ERROR))


#define njs_prototype_type(index)                                             \
    (index + NJS_OBJECT)


enum njs_constructor_e {
    NJS_CONSTRUCTOR_OBJECT =         NJS_PROTOTYPE_OBJECT,
    NJS_CONSTRUCTOR_ARRAY =          NJS_PROTOTYPE_ARRAY,
    NJS_CONSTRUCTOR_BOOLEAN =        NJS_PROTOTYPE_BOOLEAN,
    NJS_CONSTRUCTOR_NUMBER =         NJS_PROTOTYPE_NUMBER,
    NJS_CONSTRUCTOR_STRING =         NJS_PROTOTYPE_STRING,
    NJS_CONSTRUCTOR_FUNCTION =       NJS_PROTOTYPE_FUNCTION,
    NJS_CONSTRUCTOR_REGEXP =         NJS_PROTOTYPE_REGEXP,
    NJS_CONSTRUCTOR_DATE =           NJS_PROTOTYPE_DATE,
    NJS_CONSTRUCTOR_CRYPTO_HASH =    NJS_PROTOTYPE_CRYPTO_HASH,
    NJS_CONSTRUCTOR_CRYPTO_HMAC =    NJS_PROTOTYPE_CRYPTO_HMAC,
    NJS_CONSTRUCTOR_ERROR =          NJS_PROTOTYPE_ERROR,
    NJS_CONSTRUCTOR_EVAL_ERROR =     NJS_PROTOTYPE_EVAL_ERROR,
    NJS_CONSTRUCTOR_INTERNAL_ERROR = NJS_PROTOTYPE_INTERNAL_ERROR,
    NJS_CONSTRUCTOR_RANGE_ERROR =    NJS_PROTOTYPE_RANGE_ERROR,
    NJS_CONSTRUCTOR_REF_ERROR =      NJS_PROTOTYPE_REF_ERROR,
    NJS_CONSTRUCTOR_SYNTAX_ERROR =   NJS_PROTOTYPE_SYNTAX_ERROR,
    NJS_CONSTRUCTOR_TYPE_ERROR =     NJS_PROTOTYPE_TYPE_ERROR,
    NJS_CONSTRUCTOR_URI_ERROR =      NJS_PROTOTYPE_URI_ERROR,
    /* MemoryError has no its own prototype. */
    NJS_CONSTRUCTOR_MEMORY_ERROR,
#define NJS_CONSTRUCTOR_MAX    (NJS_CONSTRUCTOR_MEMORY_ERROR + 1)
};


enum njs_object_e {
    NJS_OBJECT_THIS = 0,
    NJS_OBJECT_NJS,
    NJS_OBJECT_MATH,
    NJS_OBJECT_JSON,
#define NJS_OBJECT_MAX         (NJS_OBJECT_JSON + 1)
};


enum njs_function_e {
    NJS_FUNCTION_EVAL = 0,
    NJS_FUNCTION_TO_STRING,
    NJS_FUNCTION_IS_NAN,
    NJS_FUNCTION_IS_FINITE,
    NJS_FUNCTION_PARSE_INT,
    NJS_FUNCTION_PARSE_FLOAT,
    NJS_FUNCTION_STRING_ENCODE_URI,
    NJS_FUNCTION_STRING_ENCODE_URI_COMPONENT,
    NJS_FUNCTION_STRING_DECODE_URI,
    NJS_FUNCTION_STRING_DECODE_URI_COMPONENT,
    NJS_FUNCTION_REQUIRE,
    NJS_FUNCTION_SET_TIMEOUT,
    NJS_FUNCTION_CLEAR_TIMEOUT,
#define NJS_FUNCTION_MAX       (NJS_FUNCTION_CLEAR_TIMEOUT + 1)
};


#define njs_scope_index(value, type)                                          \
    ((njs_index_t) (((value) << NJS_SCOPE_SHIFT) | (type)))

#define njs_global_scope_index(value)                                         \
    ((njs_index_t) (((value) << NJS_SCOPE_SHIFT) | NJS_SCOPE_GLOBAL))


#define NJS_INDEX_OBJECT         njs_global_scope_index(NJS_CONSTRUCTOR_OBJECT)
#define NJS_INDEX_ARRAY          njs_global_scope_index(NJS_CONSTRUCTOR_ARRAY)
#define NJS_INDEX_BOOLEAN        njs_global_scope_index(NJS_CONSTRUCTOR_BOOLEAN)
#define NJS_INDEX_NUMBER         njs_global_scope_index(NJS_CONSTRUCTOR_NUMBER)
#define NJS_INDEX_STRING         njs_global_scope_index(NJS_CONSTRUCTOR_STRING)
#define NJS_INDEX_FUNCTION                                                    \
    njs_global_scope_index(NJS_CONSTRUCTOR_FUNCTION)
#define NJS_INDEX_REGEXP         njs_global_scope_index(NJS_CONSTRUCTOR_REGEXP)
#define NJS_INDEX_DATE           njs_global_scope_index(NJS_CONSTRUCTOR_DATE)
#define NJS_INDEX_OBJECT_ERROR   njs_global_scope_index(NJS_CONSTRUCTOR_ERROR)
#define NJS_INDEX_OBJECT_EVAL_ERROR                                           \
    njs_global_scope_index(NJS_CONSTRUCTOR_EVAL_ERROR)
#define NJS_INDEX_OBJECT_INTERNAL_ERROR                                       \
    njs_global_scope_index(NJS_CONSTRUCTOR_INTERNAL_ERROR)
#define NJS_INDEX_OBJECT_RANGE_ERROR                                          \
    njs_global_scope_index(NJS_CONSTRUCTOR_RANGE_ERROR)
#define NJS_INDEX_OBJECT_REF_ERROR                                            \
    njs_global_scope_index(NJS_CONSTRUCTOR_REF_ERROR)
#define NJS_INDEX_OBJECT_SYNTAX_ERROR                                         \
    njs_global_scope_index(NJS_CONSTRUCTOR_SYNTAX_ERROR)
#define NJS_INDEX_OBJECT_TYPE_ERROR                                           \
    njs_global_scope_index(NJS_CONSTRUCTOR_TYPE_ERROR)
#define NJS_INDEX_OBJECT_URI_ERROR                                            \
    njs_global_scope_index(NJS_CONSTRUCTOR_URI_ERROR)
#define NJS_INDEX_OBJECT_MEMORY_ERROR                                         \
    njs_global_scope_index(NJS_CONSTRUCTOR_MEMORY_ERROR)

#define NJS_INDEX_GLOBAL_RETVAL  njs_global_scope_index(NJS_CONSTRUCTOR_MAX)
#define NJS_INDEX_GLOBAL_OFFSET  njs_scope_index(NJS_CONSTRUCTOR_MAX + 1, 0)


#define njs_scope_offset(index)                                               \
    ((uintptr_t) (index) & ~NJS_SCOPE_MASK)


#define njs_vmcode_operand(vm, index)                                         \
    ((njs_value_t *)                                                          \
     ((u_char *) vm->scopes[(uintptr_t) (index) & NJS_SCOPE_MASK]             \
      + njs_scope_offset(index)))


typedef struct {
    const njs_vmcode_1addr_t  *code;
    nxt_bool_t                reference;
} njs_vm_trap_t;


typedef struct {
    uint32_t                  line;
    nxt_str_t                 name;
    njs_function_lambda_t     *lambda;
} njs_function_debug_t;


struct njs_vm_s {
    /* njs_vm_t must be aligned to njs_value_t due to scratch value. */
    njs_value_t              retval;

    /*
     * The scratch value is used for lvalue operations on nonexistent
     * properties of non-object values: "a = 1; a.b++".
     */
    njs_value_t              scratch;

    u_char                   *current;

    njs_value_t              *scopes[NJS_SCOPES];

    njs_external_ptr_t       external;

    njs_native_frame_t       *top_frame;
    njs_frame_t              *active_frame;

    nxt_array_t              *external_objects; /* of njs_external_ptr_t */

    nxt_lvlhsh_t             externals_hash;
    nxt_lvlhsh_t             external_prototypes_hash;

    nxt_lvlhsh_t             variables_hash;
    nxt_lvlhsh_t             values_hash;
    nxt_lvlhsh_t             modules_hash;

    uint32_t                 event_id;
    nxt_lvlhsh_t             events_hash;
    nxt_queue_t              posted_events;

    njs_vm_opt_t             options;

    /*
     * The prototypes and constructors arrays must be together because
     * they are copied from njs_vm_shared_t by single memcpy()
     * in njs_builtin_objects_clone().
     */
    njs_object_prototype_t   prototypes[NJS_PROTOTYPE_MAX];
    njs_function_t           constructors[NJS_CONSTRUCTOR_MAX];

    nxt_mem_cache_pool_t     *mem_cache_pool;

    njs_value_t              *global_scope;
    size_t                   scope_size;
    size_t                   stack_size;

    njs_vm_shared_t          *shared;
    njs_parser_t             *parser;

    nxt_regex_context_t      *regex_context;
    nxt_regex_match_data_t   *single_match_data;

    /*
     * MemoryError is statically allocated immutable Error object
     * with the generic type NJS_OBJECT_INTERNAL_ERROR.
     */
    njs_object_t             memory_error_object;

    nxt_array_t              *code;  /* of njs_vm_code_t */

    nxt_trace_t              trace;
    nxt_random_t             random;

    nxt_array_t              *debug;
    nxt_array_t              *backtrace;

    njs_trap_t               trap:8;

    /*
     * njs_property_query() uses it to store reference to a temporary
     * PROPERTY_HANDLERs for NJS_EXTERNAL values in NJS_PROPERTY_QUERY_SET
     * and NJS_PROPERTY_QUERY_DELETE modes.
     */
    uintptr_t                stash; /* njs_property_query_t * */
};


typedef struct {
    u_char                   *start;
    u_char                   *end;
} njs_vm_code_t;


struct njs_vm_shared_s {
    nxt_lvlhsh_t             keywords_hash;
    nxt_lvlhsh_t             values_hash;
    nxt_lvlhsh_t             function_prototype_hash;
    nxt_lvlhsh_t             arguments_object_hash;

    njs_object_t             objects[NJS_OBJECT_MAX];
    njs_function_t           functions[NJS_FUNCTION_MAX];

    /*
     * The prototypes and constructors arrays must be togther because they are
     * copied to njs_vm_t by single memcpy() in njs_builtin_objects_clone().
     */
    njs_object_prototype_t   prototypes[NJS_PROTOTYPE_MAX];
    njs_function_t           constructors[NJS_CONSTRUCTOR_MAX];

    njs_regexp_pattern_t     *empty_regexp_pattern;
};


nxt_int_t njs_vmcode_interpreter(njs_vm_t *vm);

void njs_value_retain(njs_value_t *value);
void njs_value_release(njs_vm_t *vm, njs_value_t *value);

njs_ret_t njs_vmcode_object(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *inlvd2);
njs_ret_t njs_vmcode_array(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *inlvd2);
njs_ret_t njs_vmcode_function(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *invld2);
njs_ret_t njs_vmcode_arguments(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *invld2);
njs_ret_t njs_vmcode_regexp(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *invld2);
njs_ret_t njs_vmcode_object_copy(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);

njs_ret_t njs_vmcode_property_get(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property);
njs_ret_t njs_vmcode_property_set(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property);
njs_ret_t njs_vmcode_property_in(njs_vm_t *vm, njs_value_t *property,
    njs_value_t *object);
njs_ret_t njs_vmcode_property_delete(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property);
njs_ret_t njs_vmcode_property_foreach(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *invld);
njs_ret_t njs_vmcode_property_next(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *value);
njs_ret_t njs_vmcode_instance_of(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *constructor);

njs_ret_t njs_vmcode_increment(njs_vm_t *vm, njs_value_t *reference,
    njs_value_t *value);
njs_ret_t njs_vmcode_decrement(njs_vm_t *vm, njs_value_t *reference,
    njs_value_t *value);
njs_ret_t njs_vmcode_post_increment(njs_vm_t *vm, njs_value_t *reference,
    njs_value_t *value);
njs_ret_t njs_vmcode_post_decrement(njs_vm_t *vm, njs_value_t *reference,
    njs_value_t *value);
njs_ret_t njs_vmcode_typeof(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);
njs_ret_t njs_vmcode_void(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *invld2);
njs_ret_t njs_vmcode_delete(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);
njs_ret_t njs_vmcode_unary_plus(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);
njs_ret_t njs_vmcode_unary_negation(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);
njs_ret_t njs_vmcode_addition(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_substraction(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_multiplication(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_exponentiation(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_division(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_remainder(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_logical_not(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *inlvd);
njs_ret_t njs_vmcode_test_if_true(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);
njs_ret_t njs_vmcode_test_if_false(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);
njs_ret_t njs_vmcode_bitwise_not(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *inlvd);
njs_ret_t njs_vmcode_bitwise_and(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_bitwise_xor(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_bitwise_or(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_left_shift(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_right_shift(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_unsigned_right_shift(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_equal(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2);
njs_ret_t njs_vmcode_not_equal(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_less(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2);
njs_ret_t njs_vmcode_greater(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_less_or_equal(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_greater_or_equal(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_strict_equal(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_strict_not_equal(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);

njs_ret_t njs_vmcode_move(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld);

njs_ret_t njs_vmcode_jump(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *offset);
njs_ret_t njs_vmcode_if_true_jump(njs_vm_t *vm, njs_value_t *cond,
    njs_value_t *offset);
njs_ret_t njs_vmcode_if_false_jump(njs_vm_t *vm, njs_value_t *cond,
    njs_value_t *offset);
njs_ret_t njs_vmcode_if_equal_jump(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);

njs_ret_t njs_vmcode_function_frame(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *nargs);
njs_ret_t njs_vmcode_method_frame(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *method);
njs_ret_t njs_vmcode_function_call(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *retval);
njs_ret_t njs_vmcode_return(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *retval);
njs_ret_t njs_vmcode_stop(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *retval);

njs_ret_t njs_vmcode_try_start(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *offset);
njs_ret_t njs_vmcode_try_end(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *offset);
njs_ret_t njs_vmcode_throw(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *retval);
njs_ret_t njs_vmcode_catch(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *exception);
njs_ret_t njs_vmcode_finally(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *retval);

nxt_bool_t njs_values_strict_equal(const njs_value_t *val1,
    const njs_value_t *val2);

njs_ret_t njs_normalize_args(njs_vm_t *vm, njs_value_t *args,
    uint8_t *args_types, nxt_uint_t nargs);
const char *njs_type_string(njs_value_type_t type);
const char *njs_arg_type_string(uint8_t arg);

njs_ret_t njs_native_function_arguments(njs_vm_t *vm, njs_value_t *args,
    uint8_t *args_types, nxt_uint_t nargs);

nxt_int_t njs_builtin_objects_create(njs_vm_t *vm);
nxt_int_t njs_builtin_objects_clone(njs_vm_t *vm);
nxt_int_t njs_builtin_match_native_function(njs_vm_t *vm,
    njs_function_t *function, nxt_str_t *name);

nxt_array_t *njs_vm_backtrace(njs_vm_t *vm);

void *njs_lvlhsh_alloc(void *data, size_t size, nxt_uint_t nalloc);
void njs_lvlhsh_free(void *data, void *p, size_t size);


extern const njs_value_t  njs_value_void;
extern const njs_value_t  njs_value_null;
extern const njs_value_t  njs_value_false;
extern const njs_value_t  njs_value_true;
extern const njs_value_t  njs_value_zero;
extern const njs_value_t  njs_value_nan;
extern const njs_value_t  njs_value_invalid;

extern const njs_value_t  njs_string_empty;
extern const njs_value_t  njs_string_comma;
extern const njs_value_t  njs_string_void;
extern const njs_value_t  njs_string_null;
extern const njs_value_t  njs_string_false;
extern const njs_value_t  njs_string_true;
extern const njs_value_t  njs_string_native;
extern const njs_value_t  njs_string_minus_zero;
extern const njs_value_t  njs_string_minus_infinity;
extern const njs_value_t  njs_string_plus_infinity;
extern const njs_value_t  njs_string_nan;
extern const njs_value_t  njs_string_internal_error;
extern const njs_value_t  njs_string_memory_error;

extern const nxt_mem_proto_t     njs_array_mem_proto;
extern const nxt_lvlhsh_proto_t  njs_object_hash_proto;

extern const njs_vmcode_1addr_t  njs_continuation_nexus[];


#endif /* _NJS_VM_H_INCLUDED_ */
