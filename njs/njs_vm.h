
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VM_H_INCLUDED_
#define _NJS_VM_H_INCLUDED_


#define NJS_TRAP_NUMBER    -10
#define NJS_TRAP_NUMBERS   -11
#define NJS_TRAP_INCDEC    -12
#define NJS_TRAP_STRINGS   -13
#define NJS_TRAP_PROPERTY  -14
#define NJS_TRAP_LAST      NJS_TRAP_PROPERTY

#define NJS_PASS           -20


/* The order of the enum is used in njs_vmcode_typeof() */

typedef enum {
    NJS_NULL            = 0x00,
    NJS_VOID            = 0x01,

    /* The order of the above type is used in njs_is_null_or_void(). */

    NJS_BOOLEAN         = 0x02,
    /*
     * The order of the above type is used in njs_is_null_or_void_or_boolean().
     */
    NJS_NUMBER          = 0x03,
    /*
     * The order of the above type is used in njs_is_numeric().
     * Booleans, null and void values can be used in mathematical operations:
     *   a numeric value of the true value is one,
     *   a numeric value of the null and false values is zero,
     *   a numeric value of the void value is NaN.
     */
    NJS_STRING          = 0x04,

    /* The order of the above type is used in njs_is_primitive(). */

    /* The type is native code. */
    NJS_NATIVE          = 0x05,

    /* The type is external code. */
    NJS_EXTERNAL        = 0x06,

    /*
     * A special value type for uninitialized array members.
     * It is also used to detect variable non-declared explicitly
     * or implicitly and to throw ReferenceError exception.
     */
    NJS_INVALID         = 0x07,

    /*
     * The object types have the third bit set.  It is used in njs_is_object().
     * NJS_OBJECT_BOOLEAN, NJS_OBJECT_NUMBER, and NJS_OBJECT_STRING must be
     * equal to NJS_BOOLEAN, NJS_NUMBER, and NJS_STRING respectively with
     * the third bit set.  It is used in njs_primitive_prototype_get_proto()
     * and in njs_property_query().  The order of object types is used in
     * vm->prototypes and vm->functions arrays.
     */
    NJS_OBJECT          = 0x08,
    NJS_ARRAY           = 0x09,
    NJS_OBJECT_BOOLEAN  = 0x0a,
    NJS_OBJECT_NUMBER   = 0x0b,
    NJS_OBJECT_STRING   = 0x0c,
    NJS_FUNCTION        = 0x0d,
    NJS_REGEXP          = 0x0e,
} njs_value_type_t;


typedef struct njs_parser_s           njs_parser_t;

typedef njs_ret_t (*njs_getter_t) (njs_vm_t *vm, njs_value_t *obj);
typedef njs_ret_t (*njs_native_t) (njs_vm_t *vm, njs_param_t *param);


typedef struct njs_string_s           njs_string_t;
typedef struct njs_object_s           njs_object_t;
typedef struct njs_array_s            njs_array_t;
typedef struct njs_object_value_s     njs_object_value_t;
typedef struct njs_function_s         njs_function_t;
typedef struct njs_function_script_s  njs_function_script_t;
typedef struct njs_regexp_s           njs_regexp_t;
typedef struct njs_regexp_pattern_s   njs_regexp_pattern_t;
typedef struct njs_extern_s           njs_extern_t;
typedef struct njs_native_frame_s     njs_native_frame_t;


union njs_value_s {
    /*
     * The njs_value_t size is 16 bytes and must be aligned to 16 bytes
     * to provide 4 bits to encode scope in njs_index_t.  This space is
     * used to store short strings.  The maximum size of a short string
     * is 14 (NJS_STRING_SHORT).  If the short_string.size field is 15
     * (NJS_STRING_LONG) then the size is in the data.string_size field
     * and the data.u.string field points to a long string.
     *
     * The number of the string types is limited to 2 types to minimize
     * overhead of processing string fields.  It is also possible to add
     * strings with size from 14 to 254 which size and length are stored in
     * the string_size and string_length byte wide fields.  This will lessen
     * the maximum size of short string to 13.
     */
    struct {
        njs_value_type_t          type:8;  /* 4 bits */
        /*
         * The truth field is set during value assignment and then can be
         * quickly tested by logical and conditional operations regardless
         * of value type.  The truth field coincides with short_string.size
         * and short_string.length so when string size and length are zero
         * the string's value is false.
         */
        uint8_t                   truth;

        /* 0xff if u.data.string is external string. */
        uint8_t                   external0;
        uint8_t                   _spare;

        /* A long string size. */
        uint32_t                  string_size;

        union {
            double                number;
            njs_string_t          *string;
            njs_object_t          *object;
            njs_array_t           *array;
            njs_object_value_t    *object_value;
            njs_function_t        *function;
            njs_regexp_t          *regexp;
            njs_getter_t          getter;
            njs_native_t          method;
            njs_extern_t          *external;
            njs_value_t           *value;
            void                  *data;
        } u;
    } data;

    struct {
        njs_value_type_t          type:8;  /* 4 bits */

#define NJS_STRING_SHORT          14
#define NJS_STRING_LONG           15

        uint8_t                   size:4;
        uint8_t                   length:4;

        u_char                    start[NJS_STRING_SHORT];
    } short_string;

    njs_value_type_t              type:8;  /* 4 bits */
};


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
        .size = sizeof(s) - 1,                                                \
        .length = sizeof(s) - 1,                                              \
        .start = s,                                                           \
    }                                                                         \
}


/* NJS_STRING_LONG is set for both big and little endian platforms. */

#define njs_long_string(s) {                                                  \
    .data = {                                                                 \
        .type = NJS_STRING,                                                   \
        .truth = (NJS_STRING_LONG << 4) | NJS_STRING_LONG,                    \
        .string_size = sizeof(s) - 1,                                         \
        .u.string = & (njs_string_t) {                                        \
            .start = (u_char *) s,                                            \
            .length = sizeof(s) - 1,                                          \
        }                                                                     \
    }                                                                         \
}


#define njs_native_function(_function, _local_size) {                         \
    .data = {                                                                 \
        .type = NJS_FUNCTION,                                                 \
        .truth = 1,                                                           \
        .string_size = _local_size,                                           \
        .u.function = & (njs_function_t) {                                    \
            .native = 1,                                                      \
            .args_offset = 1,                                                 \
            .code.native = _function,                                         \
        }                                                                     \
    }                                                                         \
}


#define njs_getter(_getter)                                                   \
    { .data = { .type = NJS_NATIVE,                                           \
                .truth = 1,                                                   \
                .u = { .getter = _getter }                                    \
    } }


#define njs_method(_method, _size)                                            \
    { .data = { .type = NJS_NATIVE,                                           \
                .truth = 1,                                                   \
                .string_size = _size,                                         \
                .u = { .method = _method }                                    \
    } }


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
    (!njs_is_nan(num) && num != 0)


#define njs_is_numeric(value)                                                 \
    ((value)->type <= NJS_NUMBER)


#define njs_is_string(value)                                                  \
    ((value)->type == NJS_STRING)


/*
 * The truth field coincides with short_string.size and short_string.length
 * so when string size and length are zero the string's value is false and
 * otherwise is true.
 */
#define njs_string_truth(value, size)


#define njs_is_primitive(value)                                               \
    ((value)->type <= NJS_STRING)


#define njs_is_object(value)                                                  \
    (((value)->type & NJS_OBJECT) != 0)


#define njs_is_array(value)                                                   \
    ((value)->type == NJS_ARRAY)


#define njs_is_function(value)                                                \
    ((value)->type == NJS_FUNCTION)


#define njs_is_regexp(value)                                                  \
    ((value)->type == NJS_REGEXP)


#define njs_is_native(value)                                                  \
    ((value)->type == NJS_NATIVE)


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
#if (NXT_64BIT)
    uint32_t                   nargs;
#else
    uint16_t                   nargs;
#endif
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
    njs_index_t                retval;
} njs_vmcode_stop_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                index;
} njs_vmcode_validate_t;


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
    uintptr_t                  length;
} njs_vmcode_array_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_function_script_t      *function;
} njs_vmcode_function_create_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_regexp_pattern_t       *pattern;
} njs_vmcode_regexp_t;


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
    njs_index_t                each;
    njs_index_t                object;
    njs_ret_t                  offset;
} njs_vmcode_prop_start_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_index_t                object;
    njs_index_t                each;
    njs_ret_t                  offset;
} njs_vmcode_prop_each_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                value;
    njs_index_t                constructor;
    njs_index_t                object;
} njs_vmcode_instance_of_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                function;
    njs_index_t                name;
} njs_vmcode_function_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                function;
    njs_index_t                object;
    njs_index_t                method;
} njs_vmcode_method_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_index_t                function;
} njs_vmcode_call_t;


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
    NJS_SCOPE_LOCAL,
    NJS_SCOPE_GLOBAL,
    NJS_SCOPE_CALLEE_ARGUMENTS,
    NJS_SCOPE_ARGUMENTS,
    NJS_SCOPE_CLOSURE,
    NJS_SCOPE_PARENT_LOCAL,
    NJS_SCOPE_PARENT_ARGUMENTS,
    NJS_SCOPE_PARENT_CLOSURE,
} njs_scope_t;


#define NJS_SCOPES             (NJS_SCOPE_PARENT_CLOSURE + 1)

#define NJS_SCOPE_SHIFT        4
#define NJS_SCOPE_MASK         ((uintptr_t) ((1 << NJS_SCOPE_SHIFT) - 1))

#define NJS_INDEX_CACHE        NJS_SCOPE_LOCAL

#define NJS_INDEX_NONE         ((njs_index_t) 0)
#define NJS_INDEX_ERROR        ((njs_index_t) -1)
#define NJS_INDEX_THIS         ((njs_index_t) (0 | NJS_SCOPE_ARGUMENTS))


enum njs_prototypes_e {
    NJS_PROTOTYPE_OBJECT = 0,
    NJS_PROTOTYPE_ARRAY,
    NJS_PROTOTYPE_BOOLEAN,
    NJS_PROTOTYPE_NUMBER,
    NJS_PROTOTYPE_STRING,
    NJS_PROTOTYPE_FUNCTION,
    NJS_PROTOTYPE_REGEXP,
#define NJS_PROTOTYPE_MAX  (NJS_PROTOTYPE_REGEXP + 1)
};


enum njs_functions_e {
    NJS_FUNCTION_OBJECT = NJS_PROTOTYPE_OBJECT,
    NJS_FUNCTION_ARRAY = NJS_PROTOTYPE_ARRAY,
    NJS_FUNCTION_BOOLEAN = NJS_PROTOTYPE_BOOLEAN,
    NJS_FUNCTION_NUMBER = NJS_PROTOTYPE_NUMBER,
    NJS_FUNCTION_STRING = NJS_PROTOTYPE_STRING,
    NJS_FUNCTION_FUNCTION = NJS_PROTOTYPE_FUNCTION,
    NJS_FUNCTION_REGEXP = NJS_PROTOTYPE_REGEXP,

    NJS_FUNCTION_EVAL,
#define NJS_FUNCTION_MAX  (NJS_FUNCTION_EVAL + 1)
};


#define njs_scope_index(value)                                                \
    ((njs_index_t) (value << NJS_SCOPE_SHIFT))

#define njs_global_scope_index(value)                                         \
    ((njs_index_t) ((value << NJS_SCOPE_SHIFT) | NJS_SCOPE_GLOBAL))


#define NJS_INDEX_OBJECT         njs_global_scope_index(NJS_FUNCTION_OBJECT)
#define NJS_INDEX_ARRAY          njs_global_scope_index(NJS_FUNCTION_ARRAY)
#define NJS_INDEX_BOOLEAN        njs_global_scope_index(NJS_FUNCTION_BOOLEAN)
#define NJS_INDEX_NUMBER         njs_global_scope_index(NJS_FUNCTION_NUMBER)
#define NJS_INDEX_STRING         njs_global_scope_index(NJS_FUNCTION_STRING)
#define NJS_INDEX_FUNCTION       njs_global_scope_index(NJS_FUNCTION_FUNCTION)
#define NJS_INDEX_REGEXP         njs_global_scope_index(NJS_FUNCTION_REGEXP)
#define NJS_INDEX_EVAL           njs_global_scope_index(NJS_FUNCTION_EVAL)

#define NJS_INDEX_GLOBAL_OFFSET  njs_scope_index(NJS_FUNCTION_MAX)


#define njs_offset(index)                                                     \
    ((uintptr_t) (index) & ~NJS_SCOPE_MASK)


#define njs_vmcode_operand(vm, index)                                         \
    ((njs_value_t *)                                                          \
     ((u_char *) vm->scopes[(uintptr_t) (index) & NJS_SCOPE_MASK]             \
      + njs_offset(index)))


#define njs_index_size(index)                                                 \
    njs_offset(index)


typedef struct {
    const njs_vmcode_1addr_t  *code;
    nxt_bool_t                reference_value;
} njs_vm_trap_t;


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

    void                     **external;

    njs_native_frame_t       *frame;

    const njs_value_t        *exception;

    nxt_lvlhsh_t             externals_hash;
    nxt_lvlhsh_t             variables_hash;
    nxt_lvlhsh_t             functions_hash;
    nxt_lvlhsh_t             values_hash;

    njs_object_t             *prototypes;
    njs_function_t           *functions;

    nxt_mem_cache_pool_t     *mem_cache_pool;

    njs_value_t              *global_scope;
    size_t                   scope_size;

    njs_vm_shared_t          *shared;
    njs_parser_t             *parser;
    njs_regexp_pattern_t     *pattern;

    nxt_array_t              *code;  /* of njs_vm_code_t */
};


typedef struct {
    u_char                   *start;
    u_char                   *end;
} njs_vm_code_t;


struct njs_vm_shared_s {
    nxt_lvlhsh_t             keywords_hash;
    nxt_lvlhsh_t             values_hash;

    njs_object_t             *prototypes;
    njs_function_t           *functions;
};


nxt_int_t njs_vmcode_interpreter(njs_vm_t *vm);

void njs_value_retain(njs_value_t *value);
void njs_value_release(njs_vm_t *vm, njs_value_t *value);

njs_ret_t njs_vmcode_object_create(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *inlvd2);
njs_ret_t njs_vmcode_array_create(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *inlvd2);
njs_ret_t njs_vmcode_function_create(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *invld2);
njs_ret_t njs_vmcode_regexp_create(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *invld2);

njs_ret_t njs_vmcode_property_get(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property);
njs_ret_t njs_vmcode_property_set(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property);
njs_ret_t njs_vmcode_property_in(njs_vm_t *vm, njs_value_t *property,
    njs_value_t *object);
njs_ret_t njs_vmcode_property_delete(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property);
njs_ret_t njs_vmcode_property_each_start(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *invld);
njs_ret_t njs_vmcode_property_each(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *each);
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
njs_ret_t njs_vmcode_division(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_remainder(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_logical_not(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *inlvd);
njs_ret_t njs_vmcode_logical_and(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
njs_ret_t njs_vmcode_logical_or(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
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
njs_ret_t njs_vmcode_validate(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *index);

njs_ret_t njs_vmcode_jump(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *offset);
njs_ret_t njs_vmcode_if_true_jump(njs_vm_t *vm, njs_value_t *cond,
    njs_value_t *offset);
njs_ret_t njs_vmcode_if_false_jump(njs_vm_t *vm, njs_value_t *cond,
    njs_value_t *offset);

njs_ret_t njs_vmcode_function(njs_vm_t *vm, njs_value_t *name,
    njs_value_t *invld);
njs_ret_t njs_vmcode_method(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *method);
njs_ret_t njs_vmcode_call(njs_vm_t *vm, njs_value_t *func, njs_value_t *retval);
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

njs_ret_t njs_vmcode_number_primitive(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *narg);
njs_ret_t njs_vmcode_string_primitive(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *narg);
njs_ret_t njs_vmcode_restart(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *invld2);

nxt_noinline void njs_number_set(njs_value_t *value, double num);

nxt_int_t njs_shared_objects_create(njs_vm_t *vm);
nxt_int_t njs_shared_objects_clone(njs_vm_t *vm);


void *njs_lvlhsh_alloc(void *data, size_t size, nxt_uint_t nalloc);
void njs_lvlhsh_free(void *data, void *p, size_t size);


extern const njs_value_t  njs_value_void;
extern const njs_value_t  njs_value_null;
extern const njs_value_t  njs_value_false;
extern const njs_value_t  njs_value_true;
extern const njs_value_t  njs_value_zero;
extern const njs_value_t  njs_value_nan;

extern const njs_value_t  njs_string_empty;
extern const njs_value_t  njs_string_comma;
extern const njs_value_t  njs_string_void;
extern const njs_value_t  njs_string_null;
extern const njs_value_t  njs_string_false;
extern const njs_value_t  njs_string_true;
extern const njs_value_t  njs_string_native;
extern const njs_value_t  njs_string_minus_infinity;
extern const njs_value_t  njs_string_plus_infinity;
extern const njs_value_t  njs_string_nan;
extern const njs_value_t  njs_string_prototype;
extern const njs_value_t  njs_string_constructor;

extern const njs_value_t  njs_string_object_null;
extern const njs_value_t  njs_string_object_undefined;
extern const njs_value_t  njs_string_object_boolean;
extern const njs_value_t  njs_string_object_number;
extern const njs_value_t  njs_string_object_string;
extern const njs_value_t  njs_string_object_object;
extern const njs_value_t  njs_string_object_array;
extern const njs_value_t  njs_string_object_function;
extern const njs_value_t  njs_string_object_regexp;

extern const njs_value_t  njs_exception_syntax_error;
extern const njs_value_t  njs_exception_reference_error;
extern const njs_value_t  njs_exception_type_error;
extern const njs_value_t  njs_exception_range_error;
extern const njs_value_t  njs_exception_memory_error;

extern const nxt_mem_proto_t     njs_array_mem_proto;
extern const nxt_lvlhsh_proto_t  njs_object_hash_proto;


#endif /* _NJS_VM_H_INCLUDED_ */
