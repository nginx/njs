
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
#define NJS_VMCODE_1OPERAND             2
#define NJS_VMCODE_NO_OPERAND           3

#define NJS_VMCODE_NO_RETVAL            0
#define NJS_VMCODE_RETVAL               1

#define VMCODE0(n)                      (n)
#define VMCODE1(n)                      ((n) + 128)

#define NJS_VMCODE_STOP                 VMCODE0(0)
#define NJS_VMCODE_JUMP                 VMCODE0(1)
#define NJS_VMCODE_PROPERTY_SET         VMCODE0(2)
#define NJS_VMCODE_PROPERTY_ACCESSOR    VMCODE0(3)
#define NJS_VMCODE_IF_TRUE_JUMP         VMCODE0(4)
#define NJS_VMCODE_IF_FALSE_JUMP        VMCODE0(5)
#define NJS_VMCODE_IF_EQUAL_JUMP        VMCODE0(6)
#define NJS_VMCODE_PROPERTY_INIT        VMCODE0(7)
#define NJS_VMCODE_RETURN               VMCODE0(8)
#define NJS_VMCODE_FUNCTION_FRAME       VMCODE0(9)
#define NJS_VMCODE_METHOD_FRAME         VMCODE0(10)
#define NJS_VMCODE_FUNCTION_CALL        VMCODE0(11)
#define NJS_VMCODE_PROPERTY_NEXT        VMCODE0(16)
#define NJS_VMCODE_THIS                 VMCODE0(17)
#define NJS_VMCODE_ARGUMENTS            VMCODE0(18)
#define NJS_VMCODE_PROTO_INIT           VMCODE0(19)

#define NJS_VMCODE_TRY_START            VMCODE0(32)
#define NJS_VMCODE_THROW                VMCODE0(33)
#define NJS_VMCODE_TRY_BREAK            VMCODE0(34)
#define NJS_VMCODE_TRY_CONTINUE         VMCODE0(35)
#define NJS_VMCODE_TRY_END              VMCODE0(37)
#define NJS_VMCODE_CATCH                VMCODE0(38)
#define NJS_VMCODE_FINALLY              VMCODE0(39)
#define NJS_VMCODE_REFERENCE_ERROR      VMCODE0(40)

#define NJS_VMCODE_NORET                127

#define NJS_VMCODE_MOVE                 VMCODE1(0)
#define NJS_VMCODE_PROPERTY_GET         VMCODE1(1)
#define NJS_VMCODE_INCREMENT            VMCODE1(2)
#define NJS_VMCODE_POST_INCREMENT       VMCODE1(3)
#define NJS_VMCODE_DECREMENT            VMCODE1(4)
#define NJS_VMCODE_POST_DECREMENT       VMCODE1(5)
#define NJS_VMCODE_TRY_RETURN           VMCODE1(6)
#define NJS_VMCODE_GLOBAL_GET           VMCODE1(7)

#define NJS_VMCODE_LESS                 VMCODE1(8)
#define NJS_VMCODE_GREATER              VMCODE1(9)
#define NJS_VMCODE_LESS_OR_EQUAL        VMCODE1(10)
#define NJS_VMCODE_GREATER_OR_EQUAL     VMCODE1(11)
#define NJS_VMCODE_ADDITION             VMCODE1(12)
#define NJS_VMCODE_EQUAL                VMCODE1(13)
#define NJS_VMCODE_NOT_EQUAL            VMCODE1(14)

#define NJS_VMCODE_SUBSTRACTION         VMCODE1(16)
#define NJS_VMCODE_MULTIPLICATION       VMCODE1(17)
#define NJS_VMCODE_EXPONENTIATION       VMCODE1(18)
#define NJS_VMCODE_DIVISION             VMCODE1(19)
#define NJS_VMCODE_REMAINDER            VMCODE1(20)
#define NJS_VMCODE_BITWISE_AND          VMCODE1(21)
#define NJS_VMCODE_BITWISE_OR           VMCODE1(22)
#define NJS_VMCODE_BITWISE_XOR          VMCODE1(23)
#define NJS_VMCODE_LEFT_SHIFT           VMCODE1(24)
#define NJS_VMCODE_RIGHT_SHIFT          VMCODE1(25)
#define NJS_VMCODE_UNSIGNED_RIGHT_SHIFT VMCODE1(26)
#define NJS_VMCODE_OBJECT_COPY          VMCODE1(27)
#define NJS_VMCODE_TEMPLATE_LITERAL     VMCODE1(28)
#define NJS_VMCODE_PROPERTY_IN          VMCODE1(29)
#define NJS_VMCODE_PROPERTY_DELETE      VMCODE1(30)
#define NJS_VMCODE_PROPERTY_FOREACH     VMCODE1(31)

#define NJS_VMCODE_STRICT_EQUAL         VMCODE1(32)
#define NJS_VMCODE_STRICT_NOT_EQUAL     VMCODE1(33)

#define NJS_VMCODE_TEST_IF_TRUE         VMCODE1(34)
#define NJS_VMCODE_TEST_IF_FALSE        VMCODE1(35)

#define NJS_VMCODE_COALESCE             VMCODE1(36)

#define NJS_VMCODE_UNARY_PLUS           VMCODE1(37)
#define NJS_VMCODE_UNARY_NEGATION       VMCODE1(38)
#define NJS_VMCODE_BITWISE_NOT          VMCODE1(39)
#define NJS_VMCODE_LOGICAL_NOT          VMCODE1(40)
#define NJS_VMCODE_OBJECT               VMCODE1(41)
#define NJS_VMCODE_ARRAY                VMCODE1(42)
#define NJS_VMCODE_FUNCTION             VMCODE1(43)
#define NJS_VMCODE_REGEXP               VMCODE1(44)

#define NJS_VMCODE_INSTANCE_OF          VMCODE1(45)
#define NJS_VMCODE_TYPEOF               VMCODE1(46)
#define NJS_VMCODE_VOID                 VMCODE1(47)
#define NJS_VMCODE_DELETE               VMCODE1(48)

#define NJS_VMCODE_NOP                  255


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
    njs_str_t                  name;
    njs_str_t                  file;
    uint32_t                   token_line;
} njs_vmcode_reference_error_t;


njs_int_t njs_vmcode_interpreter(njs_vm_t *vm, u_char *pc);

njs_object_t *njs_function_new_object(njs_vm_t *vm, njs_value_t *constructor);


#endif /* _NJS_VMCODE_H_INCLUDED_ */
