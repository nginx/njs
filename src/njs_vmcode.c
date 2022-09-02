
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


struct njs_property_next_s {
    uint32_t     index;
    njs_array_t  *array;
};

static njs_jump_off_t njs_vmcode_object(njs_vm_t *vm);
static njs_jump_off_t njs_vmcode_array(njs_vm_t *vm, u_char *pc);
static njs_jump_off_t njs_vmcode_function(njs_vm_t *vm, u_char *pc);
static njs_jump_off_t njs_vmcode_arguments(njs_vm_t *vm, u_char *pc);
static njs_jump_off_t njs_vmcode_regexp(njs_vm_t *vm, u_char *pc);
static njs_jump_off_t njs_vmcode_template_literal(njs_vm_t *vm,
    njs_value_t *inlvd1, njs_value_t *inlvd2);
static njs_jump_off_t njs_vmcode_object_copy(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);
static njs_jump_off_t njs_vmcode_function_copy(njs_vm_t *vm, njs_value_t *value,
    njs_index_t retval);

static njs_jump_off_t njs_vmcode_property_init(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *key, njs_value_t *retval);
static njs_jump_off_t njs_vmcode_proto_init(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *key, njs_value_t *retval);
static njs_jump_off_t njs_vmcode_property_in(njs_vm_t *vm,
    njs_value_t *value, njs_value_t *key);
static njs_jump_off_t njs_vmcode_property_foreach(njs_vm_t *vm,
    njs_value_t *object, njs_value_t *invld, u_char *pc);
static njs_jump_off_t njs_vmcode_instance_of(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *constructor);
static njs_jump_off_t njs_vmcode_typeof(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *invld);
static njs_jump_off_t njs_vmcode_debugger(njs_vm_t *vm);

static njs_jump_off_t njs_vmcode_return(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *retval);
static njs_jump_off_t njs_vmcode_import(njs_vm_t *vm, njs_mod_t *module,
    njs_value_t *retval);

static njs_jump_off_t njs_vmcode_await(njs_vm_t *vm, njs_vmcode_await_t *await,
    njs_promise_capability_t *pcap, njs_async_ctx_t *actx);

static njs_jump_off_t njs_vmcode_try_start(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *offset, u_char *pc);
static njs_jump_off_t njs_vmcode_try_break(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *offset);
static njs_jump_off_t njs_vmcode_try_continue(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *offset);
static njs_jump_off_t njs_vmcode_try_end(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *offset);
static njs_jump_off_t njs_vmcode_finally(njs_vm_t *vm, njs_value_t *invld,
    njs_value_t *retval, u_char *pc);
static void njs_vmcode_error(njs_vm_t *vm, u_char *pc);

static njs_jump_off_t njs_string_concat(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
static njs_jump_off_t njs_values_equal(njs_vm_t *vm, njs_value_t *val1,
    njs_value_t *val2);
static njs_jump_off_t njs_primitive_values_compare(njs_vm_t *vm,
    njs_value_t *val1, njs_value_t *val2);
static njs_jump_off_t njs_function_frame_create(njs_vm_t *vm,
    njs_value_t *value, const njs_value_t *this, uintptr_t nargs,
    njs_bool_t ctor);


#define njs_vmcode_operand(vm, index, _retval)                                \
    do {                                                                      \
        _retval = njs_scope_valid_value(vm, index);                           \
        if (njs_slow_path(_retval == NULL)) {                                 \
            goto error;                                                       \
        }                                                                     \
    } while (0)


njs_int_t
njs_vmcode_interpreter(njs_vm_t *vm, u_char *pc, void *promise_cap,
    void *async_ctx)
{
    u_char                       *catch;
    double                       num, exponent;
    int32_t                      i32;
    uint32_t                     u32;
    njs_str_t                    string;
    njs_uint_t                   hint;
    njs_bool_t                   valid, lambda_call;
    njs_value_t                  *retval, *value1, *value2;
    njs_value_t                  *src, *s1, *s2, dst;
    njs_value_t                  *function, name;
    njs_value_t                  numeric1, numeric2, primitive1, primitive2;
    njs_frame_t                  *frame;
    njs_jump_off_t               ret;
    njs_vmcode_await_t           *await;
    njs_native_frame_t           *previous, *native;
    njs_property_next_t          *next;
    njs_vmcode_import_t          *import;
    njs_vmcode_finally_t         *finally;
    njs_vmcode_generic_t         *vmcode;
    njs_vmcode_variable_t        *var;
    njs_vmcode_move_arg_t        *move_arg;
    njs_vmcode_prop_get_t        *get;
    njs_vmcode_prop_set_t        *set;
    njs_vmcode_operation_t       op;
    njs_vmcode_prop_next_t       *pnext;
    njs_vmcode_test_jump_t       *test_jump;
    njs_vmcode_equal_jump_t      *equal;
    njs_vmcode_try_return_t      *try_return;
    njs_vmcode_method_frame_t    *method_frame;
    njs_vmcode_function_copy_t   *fcopy;
    njs_vmcode_prop_accessor_t   *accessor;
    njs_vmcode_try_trampoline_t  *try_trampoline;
    njs_vmcode_function_frame_t  *function_frame;

    njs_vmcode_debug(vm, pc, "ENTER");

next:

    for ( ;; ) {

        vmcode = (njs_vmcode_generic_t *) pc;

        /*
         * The first operand is passed as is in value2 to
         *   NJS_VMCODE_JUMP,
         *   NJS_VMCODE_IF_TRUE_JUMP,
         *   NJS_VMCODE_IF_FALSE_JUMP,
         *   NJS_VMCODE_FUNCTION_FRAME,
         *   NJS_VMCODE_FUNCTION_CALL,
         *   NJS_VMCODE_RETURN,
         *   NJS_VMCODE_TRY_START,
         *   NJS_VMCODE_TRY_CONTINUE,
         *   NJS_VMCODE_TRY_BREAK,
         *   NJS_VMCODE_TRY_END,
         *   NJS_VMCODE_CATCH,
         *   NJS_VMCODE_THROW,
         *   NJS_VMCODE_STOP.
         */
        value2 = (njs_value_t *) vmcode->operand1;
        value1 = NULL;

        switch (vmcode->code.operands) {

        case NJS_VMCODE_3OPERANDS:
            njs_vmcode_operand(vm, vmcode->operand3, value2);

            /* Fall through. */

        case NJS_VMCODE_2OPERANDS:
            njs_vmcode_operand(vm, vmcode->operand2, value1);
        }

        op = vmcode->code.operation;

        /*
         * On success an operation returns size of the bytecode,
         * a jump offset or zero after the call or return operations.
         * Jumps can return a negative offset.  Compilers can generate
         *    (ret < 0 && ret >= NJS_PREEMPT)
         * as a single unsigned comparision.
         */

#ifdef NJS_DEBUG_OPCODE
        if (vm->options.opcode_debug) {
            njs_disassemble(pc, NULL, 1, NULL);
        }
#endif

        if (op > NJS_VMCODE_NORET) {

            if (op == NJS_VMCODE_MOVE) {
                njs_vmcode_operand(vm, vmcode->operand1, retval);
                *retval = *value1;

                pc += sizeof(njs_vmcode_move_t);
                goto next;
            }

            if (op == NJS_VMCODE_PROPERTY_GET) {
                get = (njs_vmcode_prop_get_t *) pc;
                njs_vmcode_operand(vm, get->value, retval);

                ret = njs_value_property(vm, value1, value2, retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                pc += sizeof(njs_vmcode_prop_get_t);
                goto next;
            }

            switch (op) {
            case NJS_VMCODE_INCREMENT:
            case NJS_VMCODE_POST_INCREMENT:
            case NJS_VMCODE_DECREMENT:
            case NJS_VMCODE_POST_DECREMENT:
                if (njs_slow_path(!njs_is_numeric(value2))) {
                    ret = njs_value_to_numeric(vm, value2, &numeric1);
                    if (njs_slow_path(ret != NJS_OK)) {
                        goto error;
                    }

                    num = njs_number(&numeric1);

                } else {
                    num = njs_number(value2);
                }

                njs_set_number(value1,
                           num + (1 - 2 * ((op - NJS_VMCODE_INCREMENT) >> 1)));

                njs_vmcode_operand(vm, vmcode->operand1, retval);

                if (op & 1) {
                    njs_set_number(retval, num);

                } else {
                    *retval = *value1;
                }

                pc += sizeof(njs_vmcode_3addr_t);
                goto next;

            case NJS_VMCODE_GLOBAL_GET:
                get = (njs_vmcode_prop_get_t *) pc;
                njs_vmcode_operand(vm, get->value, retval);

                ret = njs_value_property(vm, value1, value2, retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                pc += sizeof(njs_vmcode_prop_get_t);

                if (ret == NJS_OK) {
                    pc += sizeof(njs_vmcode_error_t);
                }

                goto next;

            /*
             * njs_vmcode_try_return() saves a return value to use it later by
             * njs_vmcode_finally(), and jumps to the nearest try_break block.
             */
            case NJS_VMCODE_TRY_RETURN:
                njs_vmcode_operand(vm, vmcode->operand1, retval);
                *retval = *value1;

                try_return = (njs_vmcode_try_return_t *) pc;
                pc += try_return->offset;
                goto next;

            case NJS_VMCODE_LESS:
            case NJS_VMCODE_GREATER:
            case NJS_VMCODE_LESS_OR_EQUAL:
            case NJS_VMCODE_GREATER_OR_EQUAL:
            case NJS_VMCODE_ADDITION:
                if (njs_slow_path(!njs_is_primitive(value1))) {
                    hint = (op == NJS_VMCODE_ADDITION) && njs_is_date(value1);
                    ret = njs_value_to_primitive(vm, &primitive1, value1, hint);
                    if (ret != NJS_OK) {
                        goto error;
                    }

                    value1 = &primitive1;
                }

                if (njs_slow_path(!njs_is_primitive(value2))) {
                    hint = (op == NJS_VMCODE_ADDITION) && njs_is_date(value2);
                    ret = njs_value_to_primitive(vm, &primitive2, value2, hint);
                    if (ret != NJS_OK) {
                        goto error;
                    }

                    value2 = &primitive2;
                }

                if (njs_slow_path(njs_is_symbol(value1)
                                  || njs_is_symbol(value2)))
                {
                    njs_symbol_conversion_failed(vm,
                        (op == NJS_VMCODE_ADDITION) &&
                        (njs_is_string(value1) || njs_is_string(value2)));

                    goto error;
                }

                njs_vmcode_operand(vm, vmcode->operand1, retval);

                if (op == NJS_VMCODE_ADDITION) {
                    if (njs_fast_path(njs_is_numeric(value1)
                                      && njs_is_numeric(value2)))
                    {
                        njs_set_number(retval, njs_number(value1)
                                               + njs_number(value2));
                        pc += sizeof(njs_vmcode_3addr_t);
                        goto next;
                    }

                    if (njs_is_string(value1)) {
                        s1 = value1;
                        s2 = &dst;
                        src = value2;

                    } else {
                        s1 = &dst;
                        s2 = value2;
                        src = value1;
                    }

                    ret = njs_primitive_value_to_string(vm, &dst, src);
                    if (njs_slow_path(ret != NJS_OK)) {
                        goto error;
                    }

                    ret = njs_string_concat(vm, s1, s2);
                    if (njs_slow_path(ret == NJS_ERROR)) {
                        goto error;
                    }

                    *retval = vm->retval;

                    pc += ret;
                    goto next;
                }

                if ((uint8_t) (op - NJS_VMCODE_GREATER) < 2) {
                    /* NJS_VMCODE_GREATER, NJS_VMCODE_LESS_OR_EQUAL */
                    src = value1;
                    value1 = value2;
                    value2 = src;
                }

                ret = njs_primitive_values_compare(vm, value1, value2);

                if (op < NJS_VMCODE_LESS_OR_EQUAL) {
                    ret = ret > 0;

                } else {
                    ret = ret == 0;
                }

                njs_set_boolean(retval, ret);

                pc += sizeof(njs_vmcode_3addr_t);
                goto next;

            case NJS_VMCODE_EQUAL:
            case NJS_VMCODE_NOT_EQUAL:
                ret = njs_values_equal(vm, value1, value2);
                if (njs_slow_path(ret < 0)) {
                    goto error;
                }

                ret ^= op - NJS_VMCODE_EQUAL;

                njs_vmcode_operand(vm, vmcode->operand1, retval);
                njs_set_boolean(retval, ret);

                pc += sizeof(njs_vmcode_3addr_t);
                goto next;

            case NJS_VMCODE_SUBSTRACTION:
            case NJS_VMCODE_MULTIPLICATION:
            case NJS_VMCODE_EXPONENTIATION:
            case NJS_VMCODE_DIVISION:
            case NJS_VMCODE_REMAINDER:
            case NJS_VMCODE_BITWISE_AND:
            case NJS_VMCODE_BITWISE_OR:
            case NJS_VMCODE_BITWISE_XOR:
            case NJS_VMCODE_LEFT_SHIFT:
            case NJS_VMCODE_RIGHT_SHIFT:
            case NJS_VMCODE_UNSIGNED_RIGHT_SHIFT:
                if (njs_slow_path(!njs_is_numeric(value1))) {
                    ret = njs_value_to_numeric(vm, value1, &numeric1);
                    if (njs_slow_path(ret != NJS_OK)) {
                        goto error;
                    }

                    value1 = &numeric1;
                }

                if (njs_slow_path(!njs_is_numeric(value2))) {
                    ret = njs_value_to_numeric(vm, value2, &numeric2);
                    if (njs_slow_path(ret != NJS_OK)) {
                        goto error;
                    }

                    value2 = &numeric2;
                }

                num = njs_number(value1);

                njs_vmcode_operand(vm, vmcode->operand1, retval);
                pc += sizeof(njs_vmcode_3addr_t);

                switch (op) {
                case NJS_VMCODE_SUBSTRACTION:
                    num -= njs_number(value2);
                    break;

                case NJS_VMCODE_MULTIPLICATION:
                    num *= njs_number(value2);
                    break;

                case NJS_VMCODE_EXPONENTIATION:
                    exponent = njs_number(value2);

                    /*
                     * According to ES7:
                     *  1. If exponent is NaN, the result should be NaN;
                     *  2. The result of +/-1 ** +/-Infinity should be NaN.
                     */
                    valid = njs_expect(1, fabs(num) != 1
                                          || (!isnan(exponent)
                                              && !isinf(exponent)));

                    num = valid ? pow(num, exponent) : NAN;
                    break;

                case NJS_VMCODE_DIVISION:
                    num /= njs_number(value2);
                    break;

                case NJS_VMCODE_REMAINDER:
                    num = fmod(num, njs_number(value2));
                    break;

                case NJS_VMCODE_BITWISE_AND:
                case NJS_VMCODE_BITWISE_OR:
                case NJS_VMCODE_BITWISE_XOR:
                    i32 = njs_number_to_int32(njs_number(value2));

                    switch (op) {
                    case NJS_VMCODE_BITWISE_AND:
                        i32 &= njs_number_to_int32(num);
                        break;

                    case NJS_VMCODE_BITWISE_OR:
                        i32 |= njs_number_to_int32(num);
                        break;

                    case NJS_VMCODE_BITWISE_XOR:
                        i32 ^= njs_number_to_int32(num);
                        break;
                    }

                    njs_set_int32(retval, i32);
                    goto next;

                default:
                    u32 = njs_number_to_uint32(njs_number(value2)) & 0x1f;

                    switch (op) {
                    case NJS_VMCODE_LEFT_SHIFT:
                    case NJS_VMCODE_RIGHT_SHIFT:
                        i32 = njs_number_to_int32(num);

                        if (op == NJS_VMCODE_LEFT_SHIFT) {
                            /* Shifting of negative numbers is undefined. */
                            i32 = (uint32_t) i32 << u32;
                        } else {
                            i32 >>= u32;
                        }

                        njs_set_int32(retval, i32);
                        break;

                    default: /* NJS_VMCODE_UNSIGNED_RIGHT_SHIFT */
                        njs_set_uint32(retval,
                                       njs_number_to_uint32(num) >> u32);
                    }

                    goto next;
                }

                njs_set_number(retval, num);
                goto next;

            case NJS_VMCODE_OBJECT_COPY:
                ret = njs_vmcode_object_copy(vm, value1, value2);
                break;

            case NJS_VMCODE_TEMPLATE_LITERAL:
                ret = njs_vmcode_template_literal(vm, value1, value2);
                break;

            case NJS_VMCODE_PROPERTY_IN:
                ret = njs_vmcode_property_in(vm, value1, value2);
                break;

            case NJS_VMCODE_PROPERTY_DELETE:
                ret = njs_value_property_delete(vm, value1, value2, NULL, 1);
                if (njs_fast_path(ret != NJS_ERROR)) {
                    vm->retval = njs_value_true;

                    ret = sizeof(njs_vmcode_3addr_t);
                }

                break;

            case NJS_VMCODE_PROPERTY_FOREACH:
                ret = njs_vmcode_property_foreach(vm, value1, value2, pc);
                break;

            case NJS_VMCODE_STRICT_EQUAL:
            case NJS_VMCODE_STRICT_NOT_EQUAL:
                ret = njs_values_strict_equal(value1, value2);

                ret ^= op - NJS_VMCODE_STRICT_EQUAL;

                njs_vmcode_operand(vm, vmcode->operand1, retval);
                njs_set_boolean(retval, ret);

                pc += sizeof(njs_vmcode_3addr_t);
                goto next;

            case NJS_VMCODE_TEST_IF_TRUE:
            case NJS_VMCODE_TEST_IF_FALSE:
            case NJS_VMCODE_COALESCE:
                if (op == NJS_VMCODE_COALESCE) {
                    ret = !njs_is_null_or_undefined(value1);

                } else {
                    ret = njs_is_true(value1);
                    ret ^= op - NJS_VMCODE_TEST_IF_TRUE;
                }

                if (ret) {
                    test_jump = (njs_vmcode_test_jump_t *) pc;
                    ret = test_jump->offset;

                } else {
                    ret = sizeof(njs_vmcode_3addr_t);
                }

                njs_vmcode_operand(vm, vmcode->operand1, retval);
                *retval = *value1;

                pc += ret;
                goto next;

            case NJS_VMCODE_UNARY_PLUS:
            case NJS_VMCODE_UNARY_NEGATION:
            case NJS_VMCODE_BITWISE_NOT:
                if (njs_slow_path(!njs_is_numeric(value1))) {
                    ret = njs_value_to_numeric(vm, value1, &numeric1);
                    if (njs_slow_path(ret != NJS_OK)) {
                        goto error;
                    }

                    value1 = &numeric1;
                }

                num = njs_number(value1);
                njs_vmcode_operand(vm, vmcode->operand1, retval);

                switch (op) {
                case NJS_VMCODE_UNARY_NEGATION:
                    num = -num;

                    /* Fall through. */
                case NJS_VMCODE_UNARY_PLUS:
                    njs_set_number(retval, num);
                    break;

                case NJS_VMCODE_BITWISE_NOT:
                    njs_set_int32(retval, ~njs_number_to_uint32(num));
                }

                pc += sizeof(njs_vmcode_2addr_t);
                goto next;

            case NJS_VMCODE_LOGICAL_NOT:
                njs_vmcode_operand(vm, vmcode->operand1, retval);
                njs_set_boolean(retval, !njs_is_true(value1));

                pc += sizeof(njs_vmcode_2addr_t);
                goto next;

            case NJS_VMCODE_OBJECT:
                ret = njs_vmcode_object(vm);
                break;

            case NJS_VMCODE_ARRAY:
                ret = njs_vmcode_array(vm, pc);
                break;

            case NJS_VMCODE_FUNCTION:
                ret = njs_vmcode_function(vm, pc);
                break;

            case NJS_VMCODE_REGEXP:
                ret = njs_vmcode_regexp(vm, pc);
                break;

            case NJS_VMCODE_INSTANCE_OF:
                ret = njs_vmcode_instance_of(vm, value1, value2);
                break;

            case NJS_VMCODE_TYPEOF:
                ret = njs_vmcode_typeof(vm, value1, value2);
                break;

            case NJS_VMCODE_VOID:
                njs_set_undefined(&vm->retval);

                ret = sizeof(njs_vmcode_2addr_t);
                break;

            case NJS_VMCODE_DELETE:
                njs_release(vm, value1);
                vm->retval = njs_value_true;

                ret = sizeof(njs_vmcode_2addr_t);
                break;

            case NJS_VMCODE_DEBUGGER:
                ret = njs_vmcode_debugger(vm);
                break;

            default:
                njs_internal_error(vm, "%d has retval", op);
                goto error;
            }

            if (njs_slow_path(ret < 0 && ret >= NJS_PREEMPT)) {
                break;
            }

            njs_vmcode_operand(vm, vmcode->operand1, retval);
            njs_release(vm, retval);
            *retval = vm->retval;

        } else {

            switch (op) {
            case NJS_VMCODE_MOVE_ARG:
                move_arg = (njs_vmcode_move_arg_t *) pc;
                native = vm->top_frame;

                hint = move_arg->dst;

                value1 = &native->arguments_offset[hint];
                njs_vmcode_operand(vm, move_arg->src, value2);

                *value1 = *value2;

                ret = sizeof(njs_vmcode_move_arg_t);
                break;

            case NJS_VMCODE_STOP:
                njs_vmcode_operand(vm, (njs_index_t) value2, value2);
                vm->retval = *value2;

                njs_vmcode_debug(vm, pc, "EXIT STOP");

                return NJS_OK;

            case NJS_VMCODE_JUMP:
                ret = (njs_jump_off_t) value2;
                break;

            case NJS_VMCODE_PROPERTY_SET:
                set = (njs_vmcode_prop_set_t *) pc;
                njs_vmcode_operand(vm, set->value, retval);

                ret = njs_value_property_set(vm, value1, value2, retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                ret = sizeof(njs_vmcode_prop_set_t);
                break;

            case NJS_VMCODE_PROPERTY_ACCESSOR:
                accessor = (njs_vmcode_prop_accessor_t *) pc;
                njs_vmcode_operand(vm, accessor->value, function);

                ret = njs_value_to_key(vm, &name, value2);
                if (njs_slow_path(ret != NJS_OK)) {
                    njs_internal_error(vm, "failed conversion of type \"%s\" "
                                       "to string while property define",
                                       njs_type_string(value2->type));
                    goto error;
                }

                ret = njs_object_prop_define(vm, value1, &name, function,
                                             accessor->type);
                if (njs_slow_path(ret != NJS_OK)) {
                    goto error;
                }

                ret = sizeof(njs_vmcode_prop_accessor_t);
                break;

            case NJS_VMCODE_IF_TRUE_JUMP:
            case NJS_VMCODE_IF_FALSE_JUMP:
                ret = njs_is_true(value1);

                ret ^= op - NJS_VMCODE_IF_TRUE_JUMP;

                ret = ret ? (njs_jump_off_t) value2
                          : (njs_jump_off_t) sizeof(njs_vmcode_cond_jump_t);

                break;

            case NJS_VMCODE_IF_EQUAL_JUMP:
                if (njs_values_strict_equal(value1, value2)) {
                    equal = (njs_vmcode_equal_jump_t *) pc;
                    ret = equal->offset;

                } else {
                    ret = sizeof(njs_vmcode_3addr_t);
                }

                break;

            case NJS_VMCODE_PROPERTY_INIT:
                set = (njs_vmcode_prop_set_t *) pc;
                njs_vmcode_operand(vm, set->value, retval);
                ret = njs_vmcode_property_init(vm, value1, value2, retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                break;

            case NJS_VMCODE_RETURN:
                njs_vmcode_operand(vm, (njs_index_t) value2, value2);

                njs_vmcode_debug(vm, pc, "EXIT RETURN");

                return njs_vmcode_return(vm, NULL, value2);

            case NJS_VMCODE_FUNCTION_COPY:
                fcopy = (njs_vmcode_function_copy_t *) pc;
                ret = njs_vmcode_function_copy(vm, fcopy->function,
                                               fcopy->retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                break;

            case NJS_VMCODE_FUNCTION_FRAME:
                function_frame = (njs_vmcode_function_frame_t *) pc;

                /* TODO: external object instead of void this. */

                ret = njs_function_frame_create(vm, value1,
                                                &njs_value_undefined,
                                                (uintptr_t) value2,
                                                function_frame->ctor);

                if (njs_slow_path(ret != NJS_OK)) {
                    goto error;
                }

                ret = sizeof(njs_vmcode_function_frame_t);
                break;

            case NJS_VMCODE_METHOD_FRAME:
                method_frame = (njs_vmcode_method_frame_t *) pc;

                ret = njs_value_property(vm, value1, value2, &dst);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                if (njs_slow_path(!njs_is_function(&dst))) {
                    ret = njs_value_to_key(vm, &dst, value2);
                    if (njs_slow_path(ret != NJS_OK)) {
                        goto error;
                    }

                    njs_key_string_get(vm, &dst, &string);
                    njs_type_error(vm,
                               "(intermediate value)[\"%V\"] is not a function",
                               &string);
                    goto error;
                }

                ret = njs_function_frame_create(vm, &dst, value1,
                                                method_frame->nargs,
                                                method_frame->ctor);

                if (njs_slow_path(ret != NJS_OK)) {
                    goto error;
                }

                ret = sizeof(njs_vmcode_method_frame_t);
                break;

            case NJS_VMCODE_FUNCTION_CALL:
                vm->active_frame->native.pc = pc;

                njs_vmcode_operand(vm, (njs_index_t) value2, value2);

                ret = njs_function_frame_invoke(vm, value2);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                njs_vmcode_debug(vm, pc, "RESUME");

                ret = sizeof(njs_vmcode_function_call_t);
                break;

            case NJS_VMCODE_PROPERTY_NEXT:
                pnext = (njs_vmcode_prop_next_t *) pc;
                retval = njs_scope_value(vm, pnext->retval);

                njs_assert(njs_is_data(value2, NJS_DATA_TAG_FOREACH_NEXT));
                next = njs_data(value2);

                if (next->index < next->array->length) {
                    *retval = next->array->start[next->index++];

                    ret = pnext->offset;
                    break;
                }

                njs_mp_free(vm->mem_pool, next);

                ret = sizeof(njs_vmcode_prop_next_t);
                break;

            case NJS_VMCODE_ARGUMENTS:
                ret = njs_vmcode_arguments(vm, pc);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                break;

            case NJS_VMCODE_PROTO_INIT:
                set = (njs_vmcode_prop_set_t *) pc;
                njs_vmcode_operand(vm, set->value, retval);
                ret = njs_vmcode_proto_init(vm, value1, value2, retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                break;

            case NJS_VMCODE_IMPORT:
                import = (njs_vmcode_import_t *) pc;
                retval = njs_scope_value(vm, import->retval);
                ret = njs_vmcode_import(vm, import->module, retval);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                break;

            case NJS_VMCODE_AWAIT:
                await = (njs_vmcode_await_t *) pc;

                ret = njs_vmcode_await(vm, await, promise_cap, async_ctx);

                njs_vmcode_debug(vm, pc, "EXIT AWAIT");

                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                return ret;

            case NJS_VMCODE_TRY_START:
                ret = njs_vmcode_try_start(vm, value1, value2, pc);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    goto error;
                }

                break;

            case NJS_VMCODE_THROW:
                njs_vmcode_operand(vm, (njs_index_t) value2, value2);
                vm->retval = *value2;
                goto error;

            case NJS_VMCODE_TRY_BREAK:
                try_trampoline = (njs_vmcode_try_trampoline_t *) pc;
                value1 = njs_scope_value(vm, try_trampoline->exit_value);

                ret = njs_vmcode_try_break(vm, value1, value2);
                break;

            case NJS_VMCODE_TRY_CONTINUE:
                try_trampoline = (njs_vmcode_try_trampoline_t *) pc;
                value1 = njs_scope_value(vm, try_trampoline->exit_value);

                ret = njs_vmcode_try_continue(vm, value1, value2);
                break;

            case NJS_VMCODE_TRY_END:
                ret = njs_vmcode_try_end(vm, value1, value2);
                break;

            /*
             * njs_vmcode_catch() is set on the start of a "catch" block to
             * store exception and to remove a "try" block if there is no
             * "finally" block or to update a catch address to the start of
             * a "finally" block.
             * njs_vmcode_catch() is set on the start of a "finally" block
             * to store uncaught exception and to remove a "try" block.
             */
            case NJS_VMCODE_CATCH:
                *value1 = vm->retval;

                if ((njs_jump_off_t) value2 == sizeof(njs_vmcode_catch_t)) {
                    ret = njs_vmcode_try_end(vm, value1, value2);

                } else {
                    frame = (njs_frame_t *) vm->top_frame;
                    frame->exception.catch = pc + (njs_jump_off_t) value2;
                    ret = sizeof(njs_vmcode_catch_t);
                }

                break;

            case NJS_VMCODE_FINALLY:
                finally = (njs_vmcode_finally_t *) pc;
                value1 = njs_scope_value(vm, finally->exit_value);

                ret = njs_vmcode_finally(vm, value1, value2, pc);

                switch (ret) {
                case NJS_OK:

                    njs_vmcode_debug(vm, pc, "EXIT FINALLY");

                    return NJS_OK;
                case NJS_ERROR:
                    goto error;
                }

                break;

            case NJS_VMCODE_LET:
                var = (njs_vmcode_variable_t *) pc;
                value1 = njs_scope_value(vm, var->dst);

                if (njs_is_valid(value1)) {
                    value1 = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t));
                    if (njs_slow_path(value1 == NULL)) {
                        njs_memory_error(vm);
                        goto error;
                    }

                    njs_scope_value_set(vm, var->dst, value1);
                }

                njs_set_undefined(value1);

                ret = sizeof(njs_vmcode_variable_t);
                break;

            case NJS_VMCODE_LET_UPDATE:
                var = (njs_vmcode_variable_t *) pc;
                value2 = njs_scope_value(vm, var->dst);

                value1 = njs_mp_alloc(vm->mem_pool, sizeof(njs_value_t));
                if (njs_slow_path(value1 == NULL)) {
                    njs_memory_error(vm);
                    goto error;
                }

                *value1 = *value2;

                njs_scope_value_set(vm, var->dst, value1);

                ret = sizeof(njs_vmcode_variable_t);
                break;

            case NJS_VMCODE_INITIALIZATION_TEST:
                var = (njs_vmcode_variable_t *) pc;
                value1 = njs_scope_value(vm, var->dst);

                if (njs_is_valid(value1)) {
                    ret = sizeof(njs_vmcode_variable_t);
                    break;
                }

                /* Fall through. */

            case NJS_VMCODE_NOT_INITIALIZED:
                njs_reference_error(vm, "cannot access variable "
                                        "before initialization");
                goto error;

            case NJS_VMCODE_ERROR:
                njs_vmcode_error(vm, pc);
                goto error;

            case NJS_VMCODE_ASSIGNMENT_ERROR:
                njs_type_error(vm, "assignment to constant variable");
                goto error;

            default:
                njs_internal_error(vm, "%d has NO retval", op);
                goto error;
            }
        }

        pc += ret;
    }

error:

    if (njs_is_error(&vm->retval)) {
        vm->active_frame->native.pc = pc;
        (void) njs_error_stack_attach(vm, &vm->retval);
    }

    for ( ;; ) {
        native = vm->top_frame;

        if (!native->native) {
            frame = (njs_frame_t *) native;
            catch = frame->exception.catch;

            if (catch != NULL) {
                pc = catch;

                goto next;
            }
        }

        previous = native->previous;
        if (previous == NULL) {
            break;
        }

        lambda_call = (native == &vm->active_frame->native);

        njs_vm_scopes_restore(vm, native, previous);

        if (native->size != 0) {
            vm->stack_size -= native->size;
            njs_mp_free(vm->mem_pool, native);
        }

        if (lambda_call) {
            break;
        }
    }

    njs_vmcode_debug(vm, pc, "EXIT ERROR");

    return NJS_ERROR;
}


static njs_jump_off_t
njs_vmcode_object(njs_vm_t *vm)
{
    njs_object_t  *object;

    object = njs_object_alloc(vm);

    if (njs_fast_path(object != NULL)) {
        njs_set_object(&vm->retval, object);

        return sizeof(njs_vmcode_object_t);
    }

    return NJS_ERROR;
}


static njs_jump_off_t
njs_vmcode_array(njs_vm_t *vm, u_char *pc)
{
    uint32_t            length;
    njs_array_t         *array;
    njs_value_t         *value;
    njs_vmcode_array_t  *code;

    code = (njs_vmcode_array_t *) pc;

    array = njs_array_alloc(vm, 0, code->length, NJS_ARRAY_SPARE);

    if (njs_fast_path(array != NULL)) {

        if (code->ctor) {
            /* Array of the form [,,,], [1,,]. */
            if (array->object.fast_array) {
                value = array->start;
                length = array->length;

                do {
                    njs_set_invalid(value);
                    value++;
                    length--;
                } while (length != 0);
            }

        } else {
            /* Array of the form [], [,,1], [1,2,3]. */
            array->length = 0;
        }

        njs_set_array(&vm->retval, array);

        return sizeof(njs_vmcode_array_t);
    }

    return NJS_ERROR;
}


static njs_jump_off_t
njs_vmcode_function(njs_vm_t *vm, u_char *pc)
{
    njs_function_t         *function;
    njs_vmcode_function_t  *code;
    njs_function_lambda_t  *lambda;

    code = (njs_vmcode_function_t *) pc;
    lambda = code->lambda;

    function = njs_function_alloc(vm, lambda, code->async);
    if (njs_slow_path(function == NULL)) {
        return NJS_ERROR;
    }

    if (njs_function_capture_closure(vm, function, lambda) != NJS_OK) {
        return NJS_ERROR;
    }

    function->args_count = lambda->nargs - lambda->rest_parameters;

    njs_set_function(&vm->retval, function);

    return sizeof(njs_vmcode_function_t);
}


static njs_jump_off_t
njs_vmcode_arguments(njs_vm_t *vm, u_char *pc)
{
    njs_frame_t             *frame;
    njs_value_t             *value;
    njs_jump_off_t          ret;
    njs_vmcode_arguments_t  *code;

    frame = (njs_frame_t *) vm->active_frame;

    if (frame->native.arguments_object == NULL) {
        ret = njs_function_arguments_object_init(vm, &frame->native);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    code = (njs_vmcode_arguments_t *) pc;

    value = njs_scope_valid_value(vm, code->dst);
    if (njs_slow_path(value == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(value, frame->native.arguments_object);

    return sizeof(njs_vmcode_arguments_t);
}


static njs_jump_off_t
njs_vmcode_regexp(njs_vm_t *vm, u_char *pc)
{
    njs_regexp_t         *regexp;
    njs_vmcode_regexp_t  *code;

    code = (njs_vmcode_regexp_t *) pc;

    regexp = njs_regexp_alloc(vm, code->pattern);

    if (njs_fast_path(regexp != NULL)) {
        njs_set_regexp(&vm->retval, regexp);

        return sizeof(njs_vmcode_regexp_t);
    }

    return NJS_ERROR;
}


static njs_jump_off_t
njs_vmcode_template_literal(njs_vm_t *vm, njs_value_t *invld1,
    njs_value_t *retval)
{
    njs_array_t     *array;
    njs_value_t     *value;
    njs_jump_off_t  ret;

    static const njs_function_t  concat = {
          .native = 1,
          .args_offset = 1,
          .u.native = njs_string_prototype_concat
    };

    value = njs_scope_valid_value(vm, (njs_index_t) retval);

    if (!njs_is_primitive(value)) {
        array = njs_array(value);

        ret = njs_function_frame(vm, (njs_function_t *) &concat,
                                 &njs_string_empty, array->start,
                                 array->length, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_function_frame_invoke(vm, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    return sizeof(njs_vmcode_template_literal_t);
}


static njs_jump_off_t
njs_vmcode_object_copy(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    njs_object_t    *object;
    njs_function_t  *function;

    switch (value->type) {

    case NJS_OBJECT:
        object = njs_object_value_copy(vm, value);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        break;

    case NJS_FUNCTION:
        function = njs_function_value_copy(vm, value);
        if (njs_slow_path(function == NULL)) {
            return NJS_ERROR;
        }

        break;

    default:
        break;
    }

    vm->retval = *value;

    njs_retain(value);

    return sizeof(njs_vmcode_object_copy_t);
}


static njs_jump_off_t
njs_vmcode_function_copy(njs_vm_t *vm, njs_value_t *value, njs_index_t retidx)
{
    njs_value_t     *retval;
    njs_function_t  *function;

    retval = njs_scope_value(vm, retidx);

    if (!njs_is_valid(retval)) {
        *retval = *value;

        function = njs_function_value_copy(vm, retval);
        if (njs_slow_path(function == NULL)) {
            return NJS_ERROR;
        }
    }

    return sizeof(njs_vmcode_function_copy_t);
}


static njs_jump_off_t
njs_vmcode_property_init(njs_vm_t *vm, njs_value_t *value, njs_value_t *key,
    njs_value_t *init)
{
    double              num;
    uint32_t            index, size;
    njs_int_t           ret;
    njs_array_t         *array;
    njs_value_t         *val, name;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    switch (value->type) {
    case NJS_ARRAY:
        num = njs_key_to_index(key);
        if (njs_slow_path(!njs_key_is_integer_index(num, key))) {
            njs_internal_error(vm,
                               "invalid index while property initialization");
            return NJS_ERROR;
        }

        index = (uint32_t) num;
        array = value->data.u.array;

        if (index >= array->length) {
            size = index - array->length;

            ret = njs_array_expand(vm, array, 0, size + 1);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            val = &array->start[array->length];

            while (size != 0) {
                njs_set_invalid(val);
                val++;
                size--;
            }

            array->length = index + 1;
        }

        /* GC: retain. */
        array->start[index] = *init;

        break;

    case NJS_OBJECT:
        ret = njs_value_to_key(vm, &name, key);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        njs_object_property_key_set(&lhq, &name, 0);
        lhq.proto = &njs_object_hash_proto;
        lhq.pool = vm->mem_pool;

        prop = njs_object_prop_alloc(vm, &name, init, 1);
        if (njs_slow_path(prop == NULL)) {
            return NJS_ERROR;
        }

        lhq.value = prop;
        lhq.replace = 1;

        ret = njs_lvlhsh_insert(njs_object_hash(value), &lhq);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_internal_error(vm, "lvlhsh insert/replace failed");
            return NJS_ERROR;
        }

        break;

    default:
        njs_internal_error(vm, "unexpected object type \"%s\" "
                           "while property initialization",
                           njs_type_string(value->type));

        return NJS_ERROR;
    }

    return sizeof(njs_vmcode_prop_set_t);
}


static njs_jump_off_t
njs_vmcode_proto_init(njs_vm_t *vm, njs_value_t *value, njs_value_t *unused,
    njs_value_t *init)
{
    njs_object_t        *obj;
    njs_jump_off_t      ret;
    njs_object_prop_t   *prop;
    njs_lvlhsh_query_t  lhq;

    lhq.key = njs_str_value("__proto__");
    lhq.key_hash = NJS___PROTO___HASH;
    lhq.proto = &njs_object_hash_proto;
    lhq.pool = vm->mem_pool;

    obj = njs_object(value);

    ret = njs_lvlhsh_find(&obj->__proto__->shared_hash, &lhq);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    prop = lhq.value;

    if (prop->type != NJS_PROPERTY_HANDLER) {
        goto fail;
    }

    ret = prop->value.data.u.prop_handler(vm, prop, value, init, &vm->retval);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    return sizeof(njs_vmcode_prop_set_t);

fail:

    njs_internal_error(vm, "\"__proto__\" init failed");
    return NJS_ERROR;
}


static njs_jump_off_t
njs_vmcode_property_in(njs_vm_t *vm, njs_value_t *value, njs_value_t *key)
{
    njs_int_t             ret;
    njs_property_query_t  pq;

    if (njs_slow_path(njs_is_primitive(value))) {
        njs_type_error(vm, "property \"in\" on primitive %s type",
                       njs_type_string(value->type));
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_key(key))) {
        ret = njs_value_to_key(vm, key, key);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_property_query_init(&pq, NJS_PROPERTY_QUERY_GET, 0);

    ret = njs_property_query(vm, &pq, value, key);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    njs_set_boolean(&vm->retval, ret == NJS_OK);

    return sizeof(njs_vmcode_3addr_t);
}


static njs_jump_off_t
njs_vmcode_property_foreach(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *invld, u_char *pc)
{
    njs_property_next_t        *next;
    njs_vmcode_prop_foreach_t  *code;

    next = njs_mp_alloc(vm->mem_pool, sizeof(njs_property_next_t));
    if (njs_slow_path(next == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    next->index = 0;
    next->array = njs_value_enumerate(vm, object, NJS_ENUM_KEYS,
                                      NJS_ENUM_STRING, 0);
    if (njs_slow_path(next->array == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    njs_set_data(&vm->retval, next, NJS_DATA_TAG_FOREACH_NEXT);

    code = (njs_vmcode_prop_foreach_t *) pc;

    return code->offset;
}


static njs_jump_off_t
njs_vmcode_instance_of(njs_vm_t *vm, njs_value_t *object,
    njs_value_t *constructor)
{
    njs_value_t        value, bound;
    njs_object_t       *prototype, *proto;
    njs_function_t     *function;
    njs_jump_off_t     ret;
    const njs_value_t  *retval;

    static const njs_value_t prototype_string = njs_string("prototype");

    if (!njs_is_function(constructor)) {
        njs_type_error(vm, "right argument is not callable");
        return NJS_ERROR;
    }

    function = njs_function(constructor);

    if (function->bound != NULL) {
        function = function->u.bound_target;
        njs_set_function(&bound, function);
        constructor = &bound;
    }

    retval = &njs_value_false;

    if (njs_is_object(object)) {
        ret = njs_value_property(vm, constructor,
                                 njs_value_arg(&prototype_string), &value);

        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (njs_fast_path(ret == NJS_OK)) {
            if (njs_slow_path(!njs_is_object(&value))) {
                njs_type_error(vm, "Function has non-object prototype "
                               "in instanceof");
                return NJS_ERROR;
            }

            prototype = njs_object(&value);
            proto = njs_object(object);

            do {
                proto = proto->__proto__;

                if (proto == prototype) {
                    retval = &njs_value_true;
                    break;
                }

            } while (proto != NULL);
        }
    }

    vm->retval = *retval;

    return sizeof(njs_vmcode_instance_of_t);
}


static njs_jump_off_t
njs_vmcode_typeof(njs_vm_t *vm, njs_value_t *value, njs_value_t *invld)
{
    /* ECMAScript 5.1: null, array and regexp are objects. */

    static const njs_value_t  *types[NJS_VALUE_TYPE_MAX] = {
        &njs_string_object,
        &njs_string_undefined,
        &njs_string_boolean,
        &njs_string_number,
        &njs_string_symbol,
        &njs_string_string,
        &njs_string_data,
        &njs_string_external,
        &njs_string_invalid,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,
        &njs_string_undefined,

        &njs_string_object,
        &njs_string_object,
        &njs_string_function,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
        &njs_string_object,
    };

    vm->retval = *types[value->type];

    return sizeof(njs_vmcode_2addr_t);
}


static njs_jump_off_t
njs_vmcode_debugger(njs_vm_t *vm)
{
    /*
     * HOW TO DEBUG JS CODE:
     * 1) put debugger instruction when certain condition is met
     *    in the JS code:
     *    function test() {
     *      if (<>) debugger;
     *    }
     * 2) set a breakpoint to njs_vmcode_debugger() in gdb.
     *
     * To see the values of certain indices:
     * 1) use njs -d test.js to dump the byte code
     * 2) find an appropriate index around DEBUGGER instruction
     * 3) in gdb: p *njs_scope_value_get(vm, <index as a hex literal>)
     **/

    njs_set_undefined(&vm->retval);

    return sizeof(njs_vmcode_debugger_t);
}


static njs_jump_off_t
njs_string_concat(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    u_char             *start;
    size_t             size, length;
    njs_string_prop_t  string1, string2;

    (void) njs_string_prop(&string1, val1);
    (void) njs_string_prop(&string2, val2);

    length = string1.length + string2.length;

    /*
     * A result of concatenation of Byte and ASCII or UTF-8 strings
     * is a Byte string.
     */
    if (njs_is_byte_string(&string1) || njs_is_byte_string(&string2)) {
        length = 0;
    }

    size = string1.size + string2.size;

    start = njs_string_alloc(vm, &vm->retval, size, length);

    if (njs_slow_path(start == NULL)) {
        return NJS_ERROR;
    }

    (void) memcpy(start, string1.start, string1.size);
    (void) memcpy(start + string1.size, string2.start, string2.size);

    return sizeof(njs_vmcode_3addr_t);
}


static njs_jump_off_t
njs_values_equal(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    njs_int_t    ret;
    njs_bool_t   nv1, nv2;
    njs_value_t  primitive;
    njs_value_t  *hv, *lv;

again:

    nv1 = njs_is_null_or_undefined(val1);
    nv2 = njs_is_null_or_undefined(val2);

    /* Void and null are equal and not comparable with anything else. */
    if (nv1 || nv2) {
        return (nv1 && nv2);
    }

    if (njs_is_numeric(val1) && njs_is_numeric(val2)) {
        /* NaNs and Infinities are handled correctly by comparision. */
        return (njs_number(val1) == njs_number(val2));
    }

    if (val1->type == val2->type) {

        if (njs_is_string(val1)) {
            return njs_string_eq(val1, val2);
        }

        if (njs_is_symbol(val1)) {
            return njs_symbol_eq(val1, val2);
        }

        return (njs_object(val1) == njs_object(val2));
    }

    /* Sort values as: numeric < symbol < string < objects. */

    if (val1->type > val2->type) {
        hv = val1;
        lv = val2;

    } else {
        hv = val2;
        lv = val1;
    }

    /* If "lv" is an object then "hv" can only be another object. */
    if (njs_is_object(lv)) {
        return 0;
    }

    /* If "hv" is a symbol then "lv" can only be a numeric. */
    if (njs_is_symbol(hv)) {
        return 0;
    }

    /* If "hv" is a string then "lv" can be a numeric or symbol. */
    if (njs_is_string(hv)) {
        return !njs_is_symbol(lv)
            && (njs_number(lv) == njs_string_to_number(hv, 0));
    }

    /* "hv" is an object and "lv" is either a string or a symbol or a numeric. */

    ret = njs_value_to_primitive(vm, &primitive, hv, 0);
    if (ret != NJS_OK) {
        return ret;
    }

    val1 = &primitive;
    val2 = lv;

    goto again;
}


/*
 * ECMAScript 5.1: 11.8.5
 * njs_primitive_values_compare() returns
 *   1 if val1 is less than val2,
 *   0 if val1 is greater than or equal to val2,
 *  -1 if the values are not comparable.
 */

static njs_jump_off_t
njs_primitive_values_compare(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2)
{
    double   num1, num2;

    if (njs_fast_path(njs_is_numeric(val1))) {
        num1 = njs_number(val1);

        if (njs_fast_path(njs_is_numeric(val2))) {
            num2 = njs_number(val2);

        } else {
            num2 = njs_string_to_number(val2, 0);
        }

    } else if (njs_is_numeric(val2)) {
        num1 = njs_string_to_number(val1, 0);
        num2 = njs_number(val2);

    } else {
        return (njs_string_cmp(val1, val2) < 0) ? 1 : 0;
    }

    /* NaN and void values are not comparable with anything. */
    if (isnan(num1) || isnan(num2)) {
        return -1;
    }

    /* Infinities are handled correctly by comparision. */
    return (num1 < num2);
}


static njs_jump_off_t
njs_function_frame_create(njs_vm_t *vm, njs_value_t *value,
    const njs_value_t *this, uintptr_t nargs, njs_bool_t ctor)
{
    njs_value_t     val;
    njs_object_t    *object;
    njs_function_t  *function;

    if (njs_fast_path(njs_is_function(value))) {

        function = njs_function(value);

        if (ctor) {
            if (!function->ctor) {
                njs_type_error(vm, "%s is not a constructor",
                               njs_type_string(value->type));
                return NJS_ERROR;
            }

            if (!function->native) {
                object = njs_function_new_object(vm, value);
                if (njs_slow_path(object == NULL)) {
                    return NJS_ERROR;
                }

                njs_set_object(&val, object);
                this = &val;
            }
        }

        return njs_function_frame(vm, function, this, NULL, nargs, ctor);
    }

    njs_type_error(vm, "%s is not a function", njs_type_string(value->type));

    return NJS_ERROR;
}


njs_object_t *
njs_function_new_object(njs_vm_t *vm, njs_value_t *constructor)
{
    njs_value_t     proto, bound;
    njs_object_t    *object;
    njs_function_t  *function;
    njs_jump_off_t  ret;

    const njs_value_t prototype_string = njs_string("prototype");

    object = njs_object_alloc(vm);
    if (njs_slow_path(object == NULL)) {
        return NULL;
    }

    function = njs_function(constructor);

    if (function->bound != NULL) {
        function = function->u.bound_target;
        njs_set_function(&bound, function);
        constructor = &bound;
    }

    ret = njs_value_property(vm, constructor, njs_value_arg(&prototype_string),
                             &proto);

    if (njs_slow_path(ret == NJS_ERROR)) {
        return NULL;
    }

    if (njs_fast_path(njs_is_object(&proto))) {
        object->__proto__ = njs_object(&proto);
    }

    return object;
}


static njs_jump_off_t
njs_vmcode_return(njs_vm_t *vm, njs_value_t *invld, njs_value_t *retval)
{
    njs_frame_t         *frame;
    njs_native_frame_t  *previous;

    frame = (njs_frame_t *) vm->top_frame;

    if (frame->native.ctor) {
        if (njs_is_object(retval)) {
            njs_release(vm, frame->native.local[0]);

        } else {
            retval = frame->native.local[0];
        }
    }

    previous = njs_function_previous_frame(&frame->native);

    njs_vm_scopes_restore(vm, &frame->native, previous);

    *frame->native.retval = *retval;

    njs_function_frame_free(vm, &frame->native);

    return NJS_OK;
}


static njs_jump_off_t
njs_vmcode_import(njs_vm_t *vm, njs_mod_t *module, njs_value_t *retval)
{
    njs_int_t     ret;
    njs_arr_t     *m;
    njs_value_t   *value;
    njs_object_t  *object;

    if (vm->modules == NULL) {
        vm->modules = njs_arr_create(vm->mem_pool, 4, sizeof(njs_value_t));
        if (njs_slow_path(vm->modules == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        m = vm->modules;

        value = njs_arr_add_multiple(m, vm->shared->module_items);
        if (njs_slow_path(value == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        njs_memzero(m->start, m->items * sizeof(njs_value_t));
    }

    value = njs_arr_item(vm->modules, module->index);

    if (!njs_is_null(value)) {
        njs_value_assign(retval, value);
        return sizeof(njs_vmcode_import_t);
    }

    if (module->function.native) {
        njs_value_assign(value, &module->value);

        object = njs_object_value_copy(vm, value);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

    } else {
        njs_set_invalid(value);
        ret = njs_vm_invoke(vm, &module->function, NULL, 0, value);
        if (ret == NJS_ERROR) {
            return ret;
        }
    }

    njs_value_assign(retval, value);

    return sizeof(njs_vmcode_import_t);
}


static njs_jump_off_t
njs_vmcode_await(njs_vm_t *vm, njs_vmcode_await_t *await,
    njs_promise_capability_t *pcap, njs_async_ctx_t *ctx)
{
    size_t              size;
    njs_int_t           ret;
    njs_frame_t         *frame;
    njs_value_t         ctor, val, on_fulfilled, on_rejected, *value;
    njs_promise_t       *promise;
    njs_function_t      *fulfilled, *rejected;
    njs_native_frame_t  *active;

    active = &vm->active_frame->native;

    value = njs_scope_valid_value(vm, await->retval);
    if (njs_slow_path(value == NULL)) {
        njs_internal_error(vm, "await->retval is invalid");
        return NJS_ERROR;
    }

    njs_set_function(&ctor, &vm->constructors[NJS_OBJ_TYPE_PROMISE]);

    promise = njs_promise_resolve(vm, &ctor, value);
    if (njs_slow_path(promise == NULL)) {
        return NJS_ERROR;
    }

    if (ctx == NULL) {
        ctx = njs_mp_alloc(vm->mem_pool, sizeof(njs_async_ctx_t));
        if (njs_slow_path(ctx == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        size = njs_function_frame_size(active);

        fulfilled = njs_promise_create_function(vm, size);
        if (njs_slow_path(fulfilled == NULL)) {
            return NJS_ERROR;
        }

        ctx->await = fulfilled->context;
        ctx->capability = pcap;

        ret = njs_function_frame_save(vm, ctx->await, NULL);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

    } else {
        fulfilled = njs_promise_create_function(vm, 0);
        if (njs_slow_path(fulfilled == NULL)) {
            return NJS_ERROR;
        }
    }

    ctx->pc = (u_char *) await + sizeof(njs_vmcode_await_t);
    ctx->index = await->retval;

    frame = (njs_frame_t *) active;

    if (frame->exception.catch != NULL) {
        ctx->await->native.pc = frame->exception.catch;

    } else {
        ctx->await->native.pc = ctx->pc;
    }

    fulfilled->context = ctx;
    fulfilled->args_count = 1;
    fulfilled->u.native = njs_await_fulfilled;

    rejected = njs_promise_create_function(vm, 0);
    if (njs_slow_path(rejected == NULL)) {
        return NJS_ERROR;
    }

    rejected->context = ctx;
    rejected->args_count = 1;
    rejected->u.native = njs_await_rejected;

    njs_set_promise(&val, promise);
    njs_set_function(&on_fulfilled, fulfilled);
    njs_set_function(&on_rejected, rejected);

    ret = njs_promise_perform_then(vm, &val, &on_fulfilled, &on_rejected, NULL);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    (void) njs_vmcode_return(vm, NULL, &vm->retval);

    return NJS_AGAIN;
}


/*
 * njs_vmcode_try_start() is set on the start of a "try" block to create
 * a "try" block, to set a catch address to the start of a "catch" or
 * "finally" blocks and to initialize a value to track uncaught exception.
 */

static njs_jump_off_t
njs_vmcode_try_start(njs_vm_t *vm, njs_value_t *exception_value,
    njs_value_t *offset, u_char *pc)
{
    njs_value_t             *exit_value;
    njs_frame_t             *frame;
    njs_exception_t         *e;
    njs_vmcode_try_start_t  *try_start;

    frame = (njs_frame_t *) vm->top_frame;

    if (frame->exception.catch != NULL) {
        e = njs_mp_alloc(vm->mem_pool, sizeof(njs_exception_t));
        if (njs_slow_path(e == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        *e = frame->exception;
        frame->exception.next = e;
    }

    frame->exception.catch = pc + (njs_jump_off_t) offset;

    njs_set_invalid(exception_value);

    try_start = (njs_vmcode_try_start_t *) pc;
    exit_value = njs_scope_value(vm, try_start->exit_value);

    njs_set_invalid(exit_value);
    njs_number(exit_value) = 0;

    return sizeof(njs_vmcode_try_start_t);
}


/*
 * njs_vmcode_try_break() sets exit_value to INVALID 1, and jumps to
 * the nearest try_end block. The exit_value is checked by njs_vmcode_finally().
 */

static njs_jump_off_t
njs_vmcode_try_break(njs_vm_t *vm, njs_value_t *exit_value,
    njs_value_t *offset)
{
    /* exit_value can contain valid value set by vmcode_try_return. */
    if (!njs_is_valid(exit_value)) {
        njs_number(exit_value) = 1;
    }

    return (njs_jump_off_t) offset;
}


/*
 * njs_vmcode_try_continue() sets exit_value to INVALID -1, and jumps to
 * the nearest try_end block. The exit_value is checked by njs_vmcode_finally().
 */

static njs_jump_off_t
njs_vmcode_try_continue(njs_vm_t *vm, njs_value_t *exit_value,
    njs_value_t *offset)
{
    njs_number(exit_value) = -1;

    return (njs_jump_off_t) offset;
}


/*
 * njs_vmcode_try_end() is set on the end of a "try" block to remove the block.
 * It is also set on the end of a "catch" block followed by a "finally" block.
 */

static njs_jump_off_t
njs_vmcode_try_end(njs_vm_t *vm, njs_value_t *invld, njs_value_t *offset)
{
    njs_frame_t      *frame;
    njs_exception_t  *e;

    frame = (njs_frame_t *) vm->top_frame;
    e = frame->exception.next;

    if (e == NULL) {
        frame->exception.catch = NULL;

    } else {
        frame->exception = *e;
        njs_mp_free(vm->mem_pool, e);
    }

    return (njs_jump_off_t) offset;
}


/*
 * njs_vmcode_finally() is set on the end of a "finally" or a "catch" block.
 *   1) to throw uncaught exception.
 *   2) to make a jump to an enslosing loop exit if "continue" or "break"
 *      statement was used inside try block.
 *   3) to finalize "return" instruction from "try" block.
 */

static njs_jump_off_t
njs_vmcode_finally(njs_vm_t *vm, njs_value_t *invld, njs_value_t *retval,
    u_char *pc)
{
    njs_value_t           *exception_value, *exit_value;
    njs_jump_off_t        offset;
    njs_vmcode_finally_t  *finally;

    exception_value = njs_scope_value(vm, (njs_index_t) retval);

    if (njs_is_valid(exception_value)) {
        vm->retval = *exception_value;

        return NJS_ERROR;
    }

    finally = (njs_vmcode_finally_t *) pc;

    exit_value = njs_scope_value(vm, finally->exit_value);

    /*
     * exit_value is set by:
     *   vmcode_try_start to INVALID 0
     *   vmcode_try_break to INVALID 1
     *   vmcode_try_continue to INVALID -1
     *   vmcode_try_return to a valid return value
     */

    if (njs_is_valid(exit_value)) {
        return njs_vmcode_return(vm, NULL, exit_value);

    } else if (njs_number(exit_value) != 0) {
        offset = (njs_number(exit_value) > 0) ? finally->break_offset
                                              : finally->continue_offset;

        if (njs_slow_path(offset
                          < (njs_jump_off_t) sizeof(njs_vmcode_finally_t)))
        {
            njs_internal_error(vm, "unset %s offset for FINALLY block",
                               (njs_number(exit_value) > 0) ? "exit"
                                                            : "continuaion");
            return NJS_ERROR;
        }

        return offset;
    }

    return sizeof(njs_vmcode_finally_t);
}


static void
njs_vmcode_error(njs_vm_t *vm, u_char *pc)
{
    njs_vmcode_error_t  *err;

    err = (njs_vmcode_error_t *) pc;

    if (err->type == NJS_OBJ_TYPE_REF_ERROR) {
        njs_reference_error(vm, "\"%V\" is not defined", &err->u.name);

    } else {
        njs_error_fmt_new(vm, &vm->retval, err->type, "%V", &err->u.message);
    }
}
