
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
 *    -1 (NJS_ERROR/NXT_ERROR):  error or exception;
 *    -2 (NJS_AGAIN/NXT_AGAIN):  postpone nJSVM execution;
 *    -3:                        not used;
 *    -4 (NJS_STOP/NXT_DONE):    njs_vmcode_stop() has stopped execution,
 *                               execution has completed successfully;
 *    -5 .. -11:                 not used.
 */

#define NJS_STOP                 NXT_DONE

/* The last return value which preempts execution. */
#define NJS_PREEMPT              (-11)


typedef njs_ret_t (*njs_vmcode_operation_t)(njs_vm_t *vm, njs_value_t *value1,
    njs_value_t *value2);


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
    njs_index_t                exception_value;
    njs_index_t                exit_value;
} njs_vmcode_try_start_t;


typedef struct {
    njs_vmcode_t               code;
    njs_ret_t                  offset;
    njs_index_t                exit_value;
} njs_vmcode_try_trampoline_t;


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
    njs_index_t                save;
    njs_index_t                retval;
    njs_ret_t                  offset;
} njs_vmcode_try_return_t;


typedef struct {
    njs_vmcode_t               code;
    njs_index_t                retval;
    njs_index_t                exit_value;
    njs_ret_t                  continue_offset;
    njs_ret_t                  break_offset;
} njs_vmcode_finally_t;


typedef struct {
    njs_vmcode_t               code;
    nxt_str_t                  name;
    nxt_str_t                  file;
    uint32_t                   token_line;
} njs_vmcode_reference_error_t;


nxt_int_t njs_vmcode_interpreter(njs_vm_t *vm);
nxt_int_t njs_vmcode_run(njs_vm_t *vm);

njs_ret_t njs_vmcode_object(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *inlvd2);
njs_ret_t njs_vmcode_array(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *inlvd2);
njs_ret_t njs_vmcode_function(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *invld2);
njs_ret_t njs_vmcode_this(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *invld2);
njs_ret_t njs_vmcode_arguments(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *invld2);
njs_ret_t njs_vmcode_regexp(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *invld2);
njs_ret_t njs_vmcode_template_literal(njs_vm_t *vm, njs_value_t *inlvd1,
    njs_value_t *inlvd2);
njs_ret_t njs_vmcode_object_copy(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);

njs_ret_t njs_vmcode_property_get(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *property);
njs_ret_t njs_vmcode_property_init(njs_vm_t *vm, njs_value_t *object,
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
njs_ret_t njs_vmcode_try_break(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *offset);
njs_ret_t njs_vmcode_try_continue(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *offset);
njs_ret_t njs_vmcode_try_return(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *offset);
njs_ret_t njs_vmcode_try_end(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *offset);
njs_ret_t njs_vmcode_throw(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *retval);
njs_ret_t njs_vmcode_catch(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *exception);
njs_ret_t njs_vmcode_finally(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *retval);
njs_ret_t njs_vmcode_reference_error(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *invld2);

#endif /* _NJS_VMCODE_H_INCLUDED_ */
