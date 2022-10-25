
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VMCODE_H_INCLUDED_
#define _NJS_VMCODE_H_INCLUDED_


/*
 * Negative return values handled by nJSVM interpreter as special events.
 * The values must be in range from -1 to -11, because -12 is minimal jump
 * offset on 32-bit platforms.
 *    0  (NJS_OK)   :  njs_vmcode_stop() has stopped execution,
 *                          execution successfully finished
 *    -1 (NJS_ERROR):  error or exception;
 *    -2 .. -11:                 not used.
 */

/* The last return value which preempts execution. */
#define NJS_PREEMPT                     (-11)


typedef intptr_t                        njs_jump_off_t;
typedef uint8_t                         njs_vmcode_operation_t;


#define NJS_VMCODE_3OPERANDS            0
#define NJS_VMCODE_2OPERANDS            1


enum {
    NJS_VMCODE_PUT_ARG = 0,
    NJS_VMCODE_STOP,
    NJS_VMCODE_JUMP,
    NJS_VMCODE_PROPERTY_SET,
    NJS_VMCODE_PROPERTY_ACCESSOR,
    NJS_VMCODE_IF_TRUE_JUMP,
    NJS_VMCODE_IF_FALSE_JUMP,
    NJS_VMCODE_IF_EQUAL_JUMP,
    NJS_VMCODE_PROPERTY_INIT,
    NJS_VMCODE_RETURN,
    NJS_VMCODE_FUNCTION_COPY,
    NJS_VMCODE_FUNCTION_FRAME,
    NJS_VMCODE_METHOD_FRAME,
    NJS_VMCODE_FUNCTION_CALL,
    NJS_VMCODE_PROPERTY_NEXT,
    NJS_VMCODE_ARGUMENTS,
    NJS_VMCODE_PROTO_INIT,
    NJS_VMCODE_TO_PROPERTY_KEY,
    NJS_VMCODE_TO_PROPERTY_KEY_CHK,
    NJS_VMCODE_SET_FUNCTION_NAME,
    NJS_VMCODE_IMPORT,
    NJS_VMCODE_AWAIT,
    NJS_VMCODE_TRY_START,
    NJS_VMCODE_THROW,
    NJS_VMCODE_TRY_BREAK,
    NJS_VMCODE_TRY_CONTINUE,
    NJS_VMCODE_TRY_END,
    NJS_VMCODE_CATCH,
    NJS_VMCODE_FINALLY,
    NJS_VMCODE_LET,
    NJS_VMCODE_LET_UPDATE,
    NJS_VMCODE_INITIALIZATION_TEST,
    NJS_VMCODE_NOT_INITIALIZED,
    NJS_VMCODE_ASSIGNMENT_ERROR,
    NJS_VMCODE_ERROR,
    NJS_VMCODE_MOVE,
    NJS_VMCODE_PROPERTY_GET,
    NJS_VMCODE_INCREMENT,
    NJS_VMCODE_POST_INCREMENT,
    NJS_VMCODE_DECREMENT,
    NJS_VMCODE_POST_DECREMENT,
    NJS_VMCODE_TRY_RETURN,
    NJS_VMCODE_GLOBAL_GET,
    NJS_VMCODE_LESS,
    NJS_VMCODE_GREATER,
    NJS_VMCODE_LESS_OR_EQUAL,
    NJS_VMCODE_GREATER_OR_EQUAL,
    NJS_VMCODE_ADDITION,
    NJS_VMCODE_EQUAL,
    NJS_VMCODE_NOT_EQUAL,
    NJS_VMCODE_SUBSTRACTION,
    NJS_VMCODE_MULTIPLICATION,
    NJS_VMCODE_EXPONENTIATION,
    NJS_VMCODE_DIVISION,
    NJS_VMCODE_REMAINDER,
    NJS_VMCODE_BITWISE_AND,
    NJS_VMCODE_BITWISE_OR,
    NJS_VMCODE_BITWISE_XOR,
    NJS_VMCODE_LEFT_SHIFT,
    NJS_VMCODE_RIGHT_SHIFT,
    NJS_VMCODE_UNSIGNED_RIGHT_SHIFT,
    NJS_VMCODE_OBJECT_COPY,
    NJS_VMCODE_TEMPLATE_LITERAL,
    NJS_VMCODE_PROPERTY_IN,
    NJS_VMCODE_PROPERTY_DELETE,
    NJS_VMCODE_PROPERTY_FOREACH,
    NJS_VMCODE_STRICT_EQUAL,
    NJS_VMCODE_STRICT_NOT_EQUAL,
    NJS_VMCODE_TEST_IF_TRUE,
    NJS_VMCODE_TEST_IF_FALSE,
    NJS_VMCODE_COALESCE,
    NJS_VMCODE_UNARY_PLUS,
    NJS_VMCODE_UNARY_NEGATION,
    NJS_VMCODE_BITWISE_NOT,
    NJS_VMCODE_LOGICAL_NOT,
    NJS_VMCODE_OBJECT,
    NJS_VMCODE_ARRAY,
    NJS_VMCODE_FUNCTION,
    NJS_VMCODE_REGEXP,
    NJS_VMCODE_INSTANCE_OF,
    NJS_VMCODE_TYPEOF,
    NJS_VMCODE_VOID,
    NJS_VMCODE_DELETE,
    NJS_VMCODE_DEBUGGER,
    NJS_VMCODES
};


typedef struct {
    njs_vmcode_operation_t     operation;
    uint8_t                    operands;   /* 2 bits */
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
     njs_vmcode_t              code;
     njs_index_t               dst;
} njs_vmcode_this_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                dst;
} njs_vmcode_arguments_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    uintptr_t                  length;
    uint8_t                    ctor;       /* 1 bit  */
} njs_vmcode_array_t;


typedef struct {
     njs_vmcode_t              code;
     njs_index_t               retval;
} njs_vmcode_template_literal_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_function_lambda_t      *lambda;
    njs_bool_t                 async;
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
    njs_jump_off_t             offset;
} njs_vmcode_jump_t;


typedef struct {
    njs_vmcode_t               code;
    njs_jump_off_t             offset;
    njs_index_t                cond;
} njs_vmcode_cond_jump_t;


typedef struct {
    njs_vmcode_t               code;
    njs_jump_off_t             offset;
    njs_index_t                value1;
    njs_index_t                value2;
} njs_vmcode_equal_jump_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_index_t                value;
    njs_jump_off_t             offset;
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
    njs_index_t                value;
    njs_index_t                object;
    njs_index_t                property;
    uint8_t                    type;
} njs_vmcode_prop_accessor_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                next;
    njs_index_t                object;
    njs_jump_off_t             offset;
} njs_vmcode_prop_foreach_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_index_t                object;
    njs_index_t                next;
    njs_jump_off_t             offset;
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
    uint8_t                    ctor;       /* 1 bit  */
} njs_vmcode_function_frame_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                nargs;
    njs_index_t                object;
    njs_index_t                method;
    uint8_t                    ctor;       /* 1 bit  */
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
    njs_jump_off_t             offset;
    njs_index_t                exception_value;
    njs_index_t                exit_value;
} njs_vmcode_try_start_t;


typedef struct {
    njs_vmcode_t               code;
    njs_jump_off_t             offset;
    njs_index_t                exit_value;
} njs_vmcode_try_trampoline_t;


typedef struct {
    njs_vmcode_t               code;
    njs_jump_off_t             offset;
    njs_index_t                exception;
} njs_vmcode_catch_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_throw_t;


typedef struct {
    njs_vmcode_t               code;
    njs_jump_off_t             offset;
} njs_vmcode_try_end_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                save;
    njs_index_t                retval;
    njs_jump_off_t             offset;
} njs_vmcode_try_return_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_index_t                exit_value;
    njs_jump_off_t             continue_offset;
    njs_jump_off_t             break_offset;
} njs_vmcode_finally_t;


typedef struct {
    njs_vmcode_t               code;
    njs_object_type_t          type;
    union {
        njs_str_t              name;
        njs_str_t              message;
    } u;
} njs_vmcode_error_t;


typedef struct {
    njs_vmcode_t               code;
    njs_value_t                *function;
    njs_index_t                retval;
} njs_vmcode_function_copy_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_mod_t                  *module;
} njs_vmcode_import_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                dst;
} njs_vmcode_variable_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_debugger_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
} njs_vmcode_await_t;


njs_int_t njs_vmcode_interpreter(njs_vm_t *vm, u_char *pc,
    void *promise_cap, void *async_ctx);

njs_object_t *njs_function_new_object(njs_vm_t *vm, njs_value_t *constructor);

#ifdef NJS_DEBUG_OPCODE
#define njs_vmcode_debug(vm, pc, prefix) {                                    \
        if (vm->options.opcode_debug) do {                                    \
            njs_vm_code_t  *code;                                             \
                                                                              \
            code = njs_lookup_code(vm, pc);                                   \
                                                                              \
            njs_printf("%s %V\n", prefix,                                     \
                       (code != NULL) ? &code->name : &njs_entry_unknown);    \
        } while (0);                                                          \
    }

#define njs_vmcode_debug_opcode()                                             \
    if (vm->options.opcode_debug) {                                           \
        njs_disassemble(pc, NULL, 1, NULL);                                   \
    }
#else
#define njs_vmcode_debug(vm, pc, prefix)
#define njs_vmcode_debug_opcode()
#endif

#endif /* _NJS_VMCODE_H_INCLUDED_ */
