
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_function.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <string.h>


static nxt_int_t njs_generator(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_variable(njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_if_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_cond_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_while_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_do_while_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_for_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_for_in_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_assignment(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_operation_assignment(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_object(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_array(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_function(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_regexp(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_delete(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_3addr_operation(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_2addr_operation(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_inc_dec_operation(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node, nxt_bool_t post);
static nxt_int_t njs_generate_function_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_return_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_function_call(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_method_call(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_try_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_throw_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static njs_index_t njs_generator_dest_index(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_noinline njs_index_t
    njs_generator_temp_index_get(njs_parser_t *parser);
static nxt_noinline nxt_int_t
    njs_generator_children_indexes_release(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_noinline nxt_int_t njs_generator_node_index_release(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_noinline nxt_int_t njs_generator_index_release(njs_vm_t *vm,
    njs_parser_t *parser, njs_index_t index);


static nxt_int_t
njs_generator(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    nxt_int_t          ret;
    njs_parser_node_t  *left;

    if (node == NULL) {
        return NXT_OK;
    }

    switch (node->token) {

    case NJS_TOKEN_IF:
        return njs_generate_if_statement(vm, parser, node);

    case NJS_TOKEN_CONDITIONAL:
        return njs_generate_cond_expression(vm, parser, node);

    case NJS_TOKEN_WHILE:
        return njs_generate_while_statement(vm, parser, node);

    case NJS_TOKEN_DO:
        return njs_generate_do_while_statement(vm, parser, node);

    case NJS_TOKEN_FOR:
        return njs_generate_for_statement(vm, parser, node);

    case NJS_TOKEN_FOR_IN:
        return njs_generate_for_in_statement(vm, parser, node);

    case NJS_TOKEN_STATEMENT:
    case NJS_TOKEN_COMMA:

        if (node->left != NULL) {
            ret = njs_generator(vm, parser, node->left);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            node->index = node->left->index;

            ret = njs_generator_node_index_release(vm, parser, node->left);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }
        }

        ret = njs_generator(vm, parser, node->right);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        node->index = node->right->index;

        return njs_generator_node_index_release(vm, parser, node->right);

    case NJS_TOKEN_ASSIGNMENT:
        return njs_generate_assignment(vm, parser, node);

    case NJS_TOKEN_BITWISE_OR_ASSIGNMENT:
    case NJS_TOKEN_BITWISE_XOR_ASSIGNMENT:
    case NJS_TOKEN_BITWISE_AND_ASSIGNMENT:
    case NJS_TOKEN_LEFT_SHIFT_ASSIGNMENT:
    case NJS_TOKEN_RIGHT_SHIFT_ASSIGNMENT:
    case NJS_TOKEN_UNSIGNED_RIGHT_SHIFT_ASSIGNMENT:
    case NJS_TOKEN_ADDITION_ASSIGNMENT:
    case NJS_TOKEN_SUBSTRACTION_ASSIGNMENT:
    case NJS_TOKEN_MULTIPLICATION_ASSIGNMENT:
    case NJS_TOKEN_DIVISION_ASSIGNMENT:
    case NJS_TOKEN_REMAINDER_ASSIGNMENT:
        return njs_generate_operation_assignment(vm, parser, node);

    case NJS_TOKEN_IN:
        /*
         * An "in" operation is parsed as standard binary expression
         * by njs_parser_binary_expression().  However, its operands
         * should be swapped to be uniform with other property operations
         * (get/set and delete) to use the property trap.
         */
        left = node->left;
        node->left = node->right;
        node->right = left;

        /* Fall through. */

    case NJS_TOKEN_LOGICAL_OR:
    case NJS_TOKEN_LOGICAL_AND:
    case NJS_TOKEN_BITWISE_OR:
    case NJS_TOKEN_BITWISE_XOR:
    case NJS_TOKEN_BITWISE_AND:
    case NJS_TOKEN_EQUAL:
    case NJS_TOKEN_NOT_EQUAL:
    case NJS_TOKEN_STRICT_EQUAL:
    case NJS_TOKEN_STRICT_NOT_EQUAL:
    case NJS_TOKEN_INSTANCEOF:
    case NJS_TOKEN_LESS:
    case NJS_TOKEN_LESS_OR_EQUAL:
    case NJS_TOKEN_GREATER:
    case NJS_TOKEN_GREATER_OR_EQUAL:
    case NJS_TOKEN_LEFT_SHIFT:
    case NJS_TOKEN_RIGHT_SHIFT:
    case NJS_TOKEN_UNSIGNED_RIGHT_SHIFT:
    case NJS_TOKEN_ADDITION:
    case NJS_TOKEN_SUBSTRACTION:
    case NJS_TOKEN_MULTIPLICATION:
    case NJS_TOKEN_DIVISION:
    case NJS_TOKEN_REMAINDER:
    case NJS_TOKEN_PROPERTY_DELETE:
    case NJS_TOKEN_PROPERTY:
        return njs_generate_3addr_operation(vm, parser, node);

    case NJS_TOKEN_DELETE:
        return njs_generate_delete(vm, parser, node);

    case NJS_TOKEN_VOID:
    case NJS_TOKEN_TYPEOF:
    case NJS_TOKEN_UNARY_PLUS:
    case NJS_TOKEN_UNARY_NEGATION:
    case NJS_TOKEN_LOGICAL_NOT:
    case NJS_TOKEN_BITWISE_NOT:
        return njs_generate_2addr_operation(vm, parser, node);

    case NJS_TOKEN_INCREMENT:
    case NJS_TOKEN_DECREMENT:
        return njs_generate_inc_dec_operation(vm, parser, node, 0);

    case NJS_TOKEN_POST_INCREMENT:
    case NJS_TOKEN_POST_DECREMENT:
        return njs_generate_inc_dec_operation(vm, parser, node, 1);

    case NJS_TOKEN_UNDEFINED:
    case NJS_TOKEN_NULL:
    case NJS_TOKEN_BOOLEAN:
    case NJS_TOKEN_NUMBER:
    case NJS_TOKEN_STRING:
        node->index = njs_value_index(vm, parser, &node->u.value);

        if (nxt_fast_path(node->index != NJS_INDEX_NONE)) {
            return NXT_OK;
        }

        return NXT_ERROR;

    case NJS_TOKEN_OBJECT_LITERAL:
        node->index = node->u.object->index;
        return NXT_OK;

    case NJS_TOKEN_OBJECT_CREATE:
        return njs_generate_object(vm, parser, node);

    case NJS_TOKEN_ARRAY_CREATE:
        return njs_generate_array(vm, parser, node);

    case NJS_TOKEN_FUNCTION_CREATE:
        return njs_generate_function(vm, parser, node);

    case NJS_TOKEN_REGEXP_LITERAL:
        return njs_generate_regexp(vm, parser, node);

    case NJS_TOKEN_THIS:
    case NJS_TOKEN_OBJECT_CONSTRUCTOR:
    case NJS_TOKEN_ARRAY_CONSTRUCTOR:
    case NJS_TOKEN_NUMBER_CONSTRUCTOR:
    case NJS_TOKEN_BOOLEAN_CONSTRUCTOR:
    case NJS_TOKEN_STRING_CONSTRUCTOR:
    case NJS_TOKEN_FUNCTION_CONSTRUCTOR:
    case NJS_TOKEN_REGEXP_CONSTRUCTOR:
    case NJS_TOKEN_EVAL:
    case NJS_TOKEN_EXTERNAL:
        return NXT_OK;

    case NJS_TOKEN_NAME:
        return njs_generate_variable(parser, node);

    case NJS_TOKEN_FUNCTION:
        return njs_generate_function_statement(vm, parser, node);

    case NJS_TOKEN_FUNCTION_CALL:
        return njs_generate_function_call(vm, parser, node);

    case NJS_TOKEN_RETURN:
        return njs_generate_return_statement(vm, parser, node);

    case NJS_TOKEN_METHOD_CALL:
        return njs_generate_method_call(vm, parser, node);

    case NJS_TOKEN_TRY:
        return njs_generate_try_statement(vm, parser, node);

    case NJS_TOKEN_THROW:
        return njs_generate_throw_statement(vm, parser, node);

    default:
        nxt_thread_log_debug("unknown token: %d", node->token);
        vm->exception = &njs_exception_syntax_error;
        return NXT_ERROR;
    }
}


static nxt_int_t
njs_generate_variable(njs_parser_t *parser, njs_parser_node_t *node)
{
    njs_value_t            *value;
    njs_vmcode_validate_t  *validate;

    node->index = node->u.variable->index;

    if (node->state == NJS_VARIABLE_NORMAL
        && node->u.variable->state < NJS_VARIABLE_SET)
    {
        njs_generate_code(parser, njs_vmcode_validate_t, validate);
        validate->code.operation = njs_vmcode_validate;
        validate->code.operands = NJS_VMCODE_NO_OPERAND;
        validate->code.retval = NJS_VMCODE_NO_RETVAL;
        validate->index = node->index;

        value = njs_variable_value(parser, node->index);
        njs_set_invalid(value);
    }

    return NXT_OK;
}


static nxt_int_t
njs_generate_if_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    u_char                  *start;
    nxt_int_t               ret;
    njs_ret_t               *label;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_cond_jump_t, cond_jump);
    cond_jump->code.operation = njs_vmcode_if_false_jump;
    cond_jump->code.operands = NJS_VMCODE_2OPERANDS;
    cond_jump->code.retval = NJS_VMCODE_NO_RETVAL;
    cond_jump->cond = node->left->index;

    start = (u_char *) cond_jump;
    label = &cond_jump->offset;

    if (node->right->token == NJS_TOKEN_ELSE) {

        node = node->right;

        ret = njs_generator(vm, parser, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code(parser, njs_vmcode_jump_t, jump);
        jump->code.operation = njs_vmcode_jump;
        jump->code.operands = NJS_VMCODE_NO_OPERAND;
        jump->code.retval = NJS_VMCODE_NO_RETVAL;

        *label = parser->code_end - start;
        start = (u_char *) jump;
        label = &jump->offset;
    }

    ret = njs_generator(vm, parser, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    *label = parser->code_end - start;

    return NXT_OK;
}


static nxt_int_t
njs_generate_cond_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t               ret;
    njs_index_t             index, *left;
    njs_parser_node_t       *branch;
    njs_vmcode_move_t       *move;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_cond_jump_t, cond_jump);
    cond_jump->code.operation = njs_vmcode_if_false_jump;
    cond_jump->code.operands = NJS_VMCODE_2OPERANDS;
    cond_jump->code.retval = NJS_VMCODE_NO_RETVAL;
    cond_jump->cond = node->left->index;

    branch = node->right;

    ret = njs_generator(vm, parser, branch->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_move_t, move);
    move->code.operation = njs_vmcode_move;
    move->code.operands = NJS_VMCODE_2OPERANDS;
    move->code.retval = NJS_VMCODE_RETVAL;
    move->src = branch->left->index;

    left = &move->dst;

    njs_generate_code(parser, njs_vmcode_jump_t, jump);
    jump->code.operation = njs_vmcode_jump;
    jump->code.operands = NJS_VMCODE_NO_OPERAND;
    jump->code.retval = NJS_VMCODE_NO_RETVAL;

    cond_jump->offset = parser->code_end - (u_char *) cond_jump;

    ret = njs_generator(vm, parser, branch->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_move_t, move);
    move->code.operation = njs_vmcode_move;
    move->code.operands = NJS_VMCODE_2OPERANDS;
    move->code.retval = NJS_VMCODE_RETVAL;
    move->src = branch->right->index;

    jump->offset = parser->code_end - (u_char *) jump;

    index = njs_generator_dest_index(vm, parser, node);

    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return index;
    }

    if (index == NJS_INDEX_NONE) {
        node->temporary = 1;

        if (node->left->temporary) {
            index = node->left->index;

            ret = njs_generator_children_indexes_release(vm, parser, branch);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

        } else if (branch->left->temporary) {
            index = branch->left->index;

            ret = njs_generator_node_index_release(vm, parser, branch->right);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

        } else if (branch->right->temporary) {
            index = branch->right->index;

        } else {
            index = njs_generator_temp_index_get(parser);
        }

    } else {
        ret = njs_generator_children_indexes_release(vm, parser, branch);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    *left = index;
    move->dst = index;
    node->index = index;

    return NXT_OK;
}


static nxt_int_t
njs_generate_while_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    u_char                  *loop;
    nxt_int_t               ret;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    /*
     * Set a jump to the loop condition.  This jump is executed once just on
     * the loop enter and eliminates execution of one additional jump inside
     * the loop per each iteration.
     */

    njs_generate_code(parser, njs_vmcode_jump_t, jump);
    jump->code.operation = njs_vmcode_jump;
    jump->code.operands = NJS_VMCODE_NO_OPERAND;
    jump->code.retval = NJS_VMCODE_NO_RETVAL;

    /* The loop body. */

    loop = parser->code_end;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop condition. */

    jump->offset = parser->code_end - (u_char *) jump;

    ret = njs_generator(vm, parser, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_cond_jump_t, cond_jump);
    cond_jump->code.operation = njs_vmcode_if_true_jump;
    cond_jump->code.operands = NJS_VMCODE_2OPERANDS;
    cond_jump->code.retval = NJS_VMCODE_NO_RETVAL;
    cond_jump->cond = node->right->index;
    cond_jump->offset = loop - (u_char *) cond_jump;

    return NXT_OK;
}


static nxt_int_t
njs_generate_do_while_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    u_char                  *loop;
    nxt_int_t               ret;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The loop body. */

    loop = parser->code_end;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop condition. */

    ret = njs_generator(vm, parser, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_cond_jump_t, cond_jump);
    cond_jump->code.operation = njs_vmcode_if_true_jump;
    cond_jump->code.operands = NJS_VMCODE_2OPERANDS;
    cond_jump->code.retval = NJS_VMCODE_NO_RETVAL;
    cond_jump->cond = node->right->index;
    cond_jump->offset = loop - (u_char *) cond_jump;

    return NXT_OK;
}


static nxt_int_t
njs_generate_for_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    u_char                  *loop;
    nxt_int_t               ret;
    njs_parser_node_t       *condition;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    jump = NULL;

    /* The loop initialization. */

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    node = node->right;
    condition = node->left;

    if (condition != NULL) {
        /*
         * The loop condition presents so set a jump to it.  This jump is
         * executed once just after the loop initialization and eliminates
         * execution of one additional jump inside the loop per each iteration.
         */
        njs_generate_code(parser, njs_vmcode_jump_t, jump);
        jump->code.operation = njs_vmcode_jump;
        jump->code.operands = NJS_VMCODE_NO_OPERAND;
        jump->code.retval = NJS_VMCODE_NO_RETVAL;
    }

    /* The loop body. */

    loop = parser->code_end;

    node = node->right;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop update. */

    ret = njs_generator(vm, parser, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop condition. */

    if (condition != NULL) {
        jump->offset = parser->code_end - (u_char *) jump;

        ret = njs_generator(vm, parser, condition);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code(parser, njs_vmcode_cond_jump_t, cond_jump);
        cond_jump->code.operation = njs_vmcode_if_true_jump;
        cond_jump->code.operands = NJS_VMCODE_2OPERANDS;
        cond_jump->code.retval = NJS_VMCODE_NO_RETVAL;
        cond_jump->cond = condition->index;
        cond_jump->offset = loop - (u_char *) cond_jump;

    } else {
        njs_generate_code(parser, njs_vmcode_jump_t, jump);
        jump->code.operation = njs_vmcode_jump;
        jump->code.operands = NJS_VMCODE_NO_OPERAND;
        jump->code.retval = NJS_VMCODE_NO_RETVAL;
        jump->offset = loop - (u_char *) jump;
    }

    return NXT_OK;
}


static nxt_int_t
njs_generate_for_in_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    u_char                   *loop;
    nxt_int_t                ret;
    njs_index_t              index;
    njs_vmcode_prop_each_t   *prop_each;
    njs_vmcode_prop_start_t  *start;

    ret = njs_generator(vm, parser, node->left->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_prop_start_t, start);
    start->code.operation = njs_vmcode_property_each_start;
    start->code.operands = NJS_VMCODE_2OPERANDS;
    start->code.retval = NJS_VMCODE_RETVAL;
    index = njs_generator_temp_index_get(parser);
    start->each = index;
    start->object = node->left->right->index;

    /* The loop body. */

    loop = parser->code_end;

    ret = njs_generator(vm, parser, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop iterator. */

    start->offset = parser->code_end - (u_char *) start;

    ret = njs_generator(vm, parser, node->left->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_prop_each_t, prop_each);
    prop_each->code.operation = njs_vmcode_property_each;
    prop_each->code.operands = NJS_VMCODE_3OPERANDS;
    prop_each->code.retval = NJS_VMCODE_RETVAL;
    prop_each->retval = node->left->left->index;
    prop_each->object = node->left->right->index;
    prop_each->each = index;
    prop_each->offset = loop - (u_char *) prop_each;

    return njs_generator_index_release(vm, parser, index);
}


static nxt_int_t
njs_generate_assignment(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t              ret;
    njs_value_t            *value;
    njs_parser_node_t      *lvalue, *expr, *obj, *prop;
    njs_vmcode_move_t      *move;
    njs_vmcode_prop_set_t  *prop_set;

    lvalue = node->left;
    expr = node->right;
    expr->dest = NULL;

    if (lvalue->token == NJS_TOKEN_NAME) {

        lvalue->index = lvalue->u.variable->index;

        if (expr->token >= NJS_TOKEN_FIRST_CONST
            && expr->token <= NJS_TOKEN_LAST_CONST)
        {
            ret = njs_generator(vm, parser, expr);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            if (lvalue->state == NJS_VARIABLE_FIRST_ASSIGNMENT) {
                /* Use a constant value is stored as variable initial value. */
                lvalue->lvalue = NJS_LVALUE_ASSIGNED;

                value = njs_variable_value(parser, lvalue->index);
                *value = expr->u.value;
                node->index = expr->index;

                return NXT_OK;
            }
        }

        expr->dest = lvalue;

        ret = njs_generator(vm, parser, expr);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        /*
         * lvalue and expression indexes are equal if the expression is an
         * empty object or expression result is stored directly in variable.
         */
        if (lvalue->index != expr->index) {
            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            move->dst = lvalue->index;
            move->src = expr->index;
        }

        node->index = expr->index;

        return njs_generator_node_index_release(vm, parser, expr);
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    ret = njs_generator(vm, parser, lvalue->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Property. */

    ret = njs_generator(vm, parser, lvalue->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (nxt_slow_path(njs_parser_has_side_effect(expr))) {
        /* Preserve object and property if they can be changed by expression. */

        obj = lvalue->left;

        if (obj->token == NJS_TOKEN_NAME) {
            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            move->dst = njs_generator_temp_index_get(parser);
            move->src = obj->index;
            obj->index = move->dst;
            obj->temporary = 1;
        }

        prop = lvalue->right;

        if (prop->token == NJS_TOKEN_NAME) {
            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            move->dst = njs_generator_temp_index_get(parser);
            move->src = prop->index;
            prop->index = move->dst;
            prop->temporary = 1;
        }
    }

    ret = njs_generator(vm, parser, expr);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_prop_set_t, prop_set);
    prop_set->code.operation = njs_vmcode_property_set;
    prop_set->code.operands = NJS_VMCODE_3OPERANDS;
    prop_set->code.retval = NJS_VMCODE_NO_RETVAL;
    node->index = expr->index;
    prop_set->value = expr->index;
    prop_set->object = lvalue->left->index;
    prop_set->property = lvalue->right->index;

    return njs_generator_children_indexes_release(vm, parser, node);
}


static nxt_int_t
njs_generate_operation_assignment(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t              ret;
    njs_index_t            index;
    njs_parser_node_t      *lvalue, *expr;
    njs_vmcode_move_t      *move;
    njs_vmcode_3addr_t     *code;
    njs_vmcode_prop_get_t  *prop_get;
    njs_vmcode_prop_set_t  *prop_set;

    lvalue = node->left;

    if (lvalue->token == NJS_TOKEN_NAME) {
        ret = njs_generate_variable(parser, lvalue);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        index = lvalue->index;
        node->index = index;
        expr = node->right;

        if (nxt_slow_path(njs_parser_has_side_effect(expr))) {
            /* Preserve variable value if it may be changed by expression. */
            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            index = njs_generator_temp_index_get(parser);
            move->dst = index;
            move->src = lvalue->index;
            lvalue->temporary = 1;
        }

        ret = njs_generator(vm, parser, expr);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code(parser, njs_vmcode_3addr_t, code);
        code->code.operation = node->u.operation;
        code->code.operands = NJS_VMCODE_3OPERANDS;
        code->code.retval = NJS_VMCODE_RETVAL;
        code->dst = lvalue->index;
        code->src1 = index;
        code->src2 = expr->index;

        return njs_generator_node_index_release(vm, parser, expr);
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    ret = njs_generator(vm, parser, lvalue->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Property. */

    ret = njs_generator(vm, parser, lvalue->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_prop_get_t, prop_get);
    prop_get->code.operation = njs_vmcode_property_get;
    prop_get->code.operands = NJS_VMCODE_3OPERANDS;
    prop_get->code.retval = NJS_VMCODE_RETVAL;
    index = njs_generator_temp_index_get(parser);
    prop_get->value = index;
    node->index = index;
    prop_get->object = lvalue->left->index;
    prop_get->property = lvalue->right->index;

    expr = node->right;

    ret = njs_generator(vm, parser, expr);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_3addr_t, code);
    code->code.operation = node->u.operation;
    code->code.operands = NJS_VMCODE_3OPERANDS;
    code->code.retval = NJS_VMCODE_RETVAL;
    code->dst = index;
    code->src1 = index;
    code->src2 = expr->index;

    njs_generate_code(parser, njs_vmcode_prop_set_t, prop_set);
    prop_set->code.operation = njs_vmcode_property_set;
    prop_set->code.operands = NJS_VMCODE_3OPERANDS;
    prop_set->code.retval = NJS_VMCODE_NO_RETVAL;
    prop_set->value = index;
    prop_set->object = lvalue->left->index;
    prop_set->property = lvalue->right->index;

    ret = njs_generator_node_index_release(vm, parser, expr);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return njs_generator_children_indexes_release(vm, parser, lvalue);
}


static nxt_int_t
njs_generate_object(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    nxt_int_t            ret;
    njs_index_t          index;
    njs_vmcode_object_t  *obj;

    index = NJS_INDEX_NONE;

    if (node->left == NULL) {
        /* An empty object, try to assign directly to variable. */

        index = njs_generator_dest_index(vm, parser, node);
        if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
            return index;
        }
    }

    if (index == NJS_INDEX_NONE) {
        index = njs_generator_temp_index_get(parser);
    }

    njs_generate_code(parser, njs_vmcode_object_t, obj);
    obj->code.operation = njs_vmcode_object_create;
    obj->code.operands = NJS_VMCODE_1OPERAND;
    obj->code.retval = NJS_VMCODE_RETVAL;
    obj->retval = index;

    node->index = index;

    if (node->left != NULL) {
        /* Initialize object. */

        ret = njs_generator(vm, parser, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        /*
         * Mark the node as a node with temporary result to allow reuse
         * its index.  The node can not be marked earlier because this
         * index may be used during initialization of the object.
         */
        node->temporary = 1;
    }

    nxt_thread_log_debug("OBJECT %p", node->index);

    return NXT_OK;
}


static nxt_int_t
njs_generate_array(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    nxt_int_t           ret;
    njs_index_t         index;
    njs_vmcode_array_t  *array;

    index = NJS_INDEX_NONE;

    if (node->left == NULL) {
        /* An empty object, try to assign directly to variable. */

        index = njs_generator_dest_index(vm, parser, node);
        if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
            return index;
        }
    }

    if (index == NJS_INDEX_NONE) {
        index = njs_generator_temp_index_get(parser);
    }

    njs_generate_code(parser, njs_vmcode_array_t, array);
    array->code.operation = njs_vmcode_array_create;
    array->code.operands = NJS_VMCODE_1OPERAND;
    array->code.retval = NJS_VMCODE_RETVAL;
    array->retval = index;
    array->length = node->u.length;

    node->index = index;

    if (node->left != NULL) {
        /* Initialize object. */

        ret = njs_generator(vm, parser, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        /*
         * Mark the node as a node with temporary result to allow reuse
         * its index.  The node can not be marked earlier because this
         * index may be used during initialization of the object.
         */
        node->temporary = 1;
    }

    nxt_thread_log_debug("ARRAY %p", node->index);

    return NXT_OK;
}


static nxt_int_t
njs_generate_function(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t                     ret;
    njs_index_t                   index;
    njs_parser_node_t             *body;
    njs_function_script_t         *func;
    njs_vmcode_operation_t        last;
    njs_vmcode_function_create_t  *function;

    body = node->right;

    if (body != NULL
        && body->right != NULL
        && body->right->token == NJS_TOKEN_RETURN)
    {
        last = NULL;

    } else {
        last = njs_vmcode_return;
    }

    func = node->u.value.data.u.data;

    ret = njs_generate_scope(vm, func->u.parser, body, last);

    if (nxt_fast_path(ret == NXT_OK)) {
        func->local_size = func->u.parser->scope_size;
        func->spare_size = func->u.parser->method_arguments_size;
        func->local_scope = func->u.parser->local_scope;
        func->u.code = func->u.parser->code_start;

        /* Try to assign directly to variable. */

        index = njs_generator_dest_index(vm, parser, node);
        if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
            return index;
        }

        if (index == NJS_INDEX_NONE) {
            index = njs_generator_temp_index_get(parser);
        }

        njs_generate_code(parser, njs_vmcode_function_create_t, function);
        function->code.operation = njs_vmcode_function_create;
        function->code.operands = NJS_VMCODE_1OPERAND;
        function->code.retval = NJS_VMCODE_RETVAL;
        function->retval = index;
        function->function = func;

        node->index = index;

        nxt_thread_log_debug("FUNCTION %p", node->index);
    }

    return ret;
}


static nxt_int_t
njs_generate_regexp(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    njs_index_t          index;
    njs_vmcode_regexp_t  *regexp;

    /* Try to assign directly to variable. */

    index = njs_generator_dest_index(vm, parser, node);
    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return index;
    }

    if (index == NJS_INDEX_NONE) {
        index = njs_generator_temp_index_get(parser);
    }

    njs_generate_code(parser, njs_vmcode_regexp_t, regexp);
    regexp->code.operation = njs_vmcode_regexp_create;
    regexp->code.operands = NJS_VMCODE_1OPERAND;
    regexp->code.retval = NJS_VMCODE_RETVAL;
    regexp->retval = index;
    regexp->pattern = node->u.value.data.u.data;

    node->index = index;

    nxt_thread_log_debug("REGEXP %p", node->index);

    return NXT_OK;
}


static nxt_int_t
njs_generate_delete(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    double              n;
    nxt_int_t           ret;
    njs_index_t         index;
    njs_parser_node_t   *left;
    njs_vmcode_2addr_t  *delete;

    left = node->left;

    /*
     * The "delete" operator returns "false" for "undefined", "NaN", and
     * "Infinity" constants but not for values, expressions and "-Infinity".
     */

    switch (left->token) {

    case NJS_TOKEN_NAME:
        if (left->u.variable->state == NJS_VARIABLE_DECLARED) {
            index = njs_value_index(vm, parser, &njs_value_false);
            goto done;
        }

        /* A property of the global object. */

        node->temporary = 1;
        node->index = njs_generator_temp_index_get(parser);

        njs_generate_code(parser, njs_vmcode_2addr_t, delete);
        delete->code.operation = njs_vmcode_delete;
        delete->code.operands = NJS_VMCODE_2OPERANDS;
        delete->code.retval = NJS_VMCODE_RETVAL;
        delete->dst = node->index;
        delete->src = left->u.variable->index;

        return NXT_OK;

    case NJS_TOKEN_NUMBER:
        n = left->u.value.data.u.number;

        if (!njs_is_nan(n) && !(njs_is_infinity(n) && n > 0.0)) {
            break;
        }

        /* Fall through. */

    case NJS_TOKEN_UNDEFINED:
        index = njs_value_index(vm, parser, &njs_value_false);
        goto done;

    default:
        ret = njs_generator(vm, parser, left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        ret = njs_generator_node_index_release(vm, parser, left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        break;
    }

    index = njs_value_index(vm, parser, &njs_value_true);

done:

    node->index = index;

    return NXT_OK;
}


static nxt_int_t
njs_generate_3addr_operation(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t           ret;
    njs_index_t         index;
    njs_parser_node_t   *left, *right;
    njs_vmcode_move_t   *move;
    njs_vmcode_3addr_t  *code;

    left = node->left;

    ret = njs_generator(vm, parser, left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    right = node->right;

    if (left->token == NJS_TOKEN_NAME) {

        if (nxt_slow_path(njs_parser_has_side_effect(right))) {
            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            move->src = left->index;
            left->index = njs_generator_temp_index_get(parser);
            move->dst = left->index;

            left->temporary = 1;
        }
    }

    ret = njs_generator(vm, parser, right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_3addr_t, code);
    code->code.operation = node->u.operation;
    code->code.operands = NJS_VMCODE_3OPERANDS;
    code->code.retval = NJS_VMCODE_RETVAL;
    code->src1 = left->index;
    code->src2 = right->index;

    index = njs_generator_dest_index(vm, parser, node);

    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return index;
    }

    if (index == NJS_INDEX_NONE) {
        node->temporary = 1;

        if (left->temporary) {
            index = left->index;

            ret = njs_generator_node_index_release(vm, parser, right);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

        } else if (right->temporary) {
            index = right->index;

        } else {
            index = njs_generator_temp_index_get(parser);
        }
    }

    node->index = index;
    code->dst = index;

    nxt_thread_log_debug("CODE3  %p, %p, %p",
                         code->dst, code->src1, code->src2);

    return NXT_OK;
}


static nxt_int_t
njs_generate_2addr_operation(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t           ret;
    njs_index_t         index;
    njs_vmcode_2addr_t  *code;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_2addr_t, code);
    code->code.operation = node->u.operation;
    code->code.operands = NJS_VMCODE_2OPERANDS;
    code->code.retval = NJS_VMCODE_RETVAL;
    code->src = node->left->index;

    index = njs_generator_dest_index(vm, parser, node);

    if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
        return index;
    }

    if (index == NJS_INDEX_NONE) {
        node->temporary = 1;

        if (node->left->temporary) {
            index = node->left->index;

        } else {
            index = njs_generator_temp_index_get(parser);
        }
    }

    node->index = index;
    code->dst = index;

    nxt_thread_log_debug("CODE2  %p, %p", code->dst, code->src);

    return NXT_OK;
}


static nxt_int_t
njs_generate_inc_dec_operation(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node, nxt_bool_t post)
{
    nxt_int_t              ret;
    njs_index_t            index, dest_index;
    njs_parser_node_t      *lvalue;
    njs_vmcode_3addr_t     *code;
    njs_vmcode_prop_get_t  *prop_get;
    njs_vmcode_prop_set_t  *prop_set;

    lvalue = node->left;

    if (lvalue->token == NJS_TOKEN_NAME) {

        ret = njs_generate_variable(parser, lvalue);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        index = njs_generator_dest_index(vm, parser, node);

        if (nxt_slow_path(index == NJS_INDEX_ERROR)) {
            return index;
        }

        if (index == NJS_INDEX_NONE) {
            node->temporary = 1;
            index = njs_generator_temp_index_get(parser);
        }

        node->index = index;

        njs_generate_code(parser, njs_vmcode_3addr_t, code);
        code->code.operation = node->u.operation;
        code->code.operands = NJS_VMCODE_3OPERANDS;
        code->code.retval = NJS_VMCODE_RETVAL;
        code->dst = index;
        code->src1 = lvalue->index;
        code->src2 = lvalue->index;

        return NXT_OK;
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    ret = njs_generator(vm, parser, lvalue->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Property. */

    ret = njs_generator(vm, parser, lvalue->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    dest_index = njs_generator_dest_index(vm, parser, node);

    if (nxt_slow_path(dest_index == NJS_INDEX_ERROR)) {
        return dest_index;
    }

    if (dest_index == NJS_INDEX_NONE
        || dest_index == lvalue->left->index
        || dest_index == lvalue->right->index)
    {
        node->temporary = 1;
        dest_index = njs_generator_temp_index_get(parser);
    }

    node->index = dest_index;

    index = post ? njs_generator_temp_index_get(parser) : dest_index;

    njs_generate_code(parser, njs_vmcode_prop_get_t, prop_get);
    prop_get->code.operation = njs_vmcode_property_get;
    prop_get->code.operands = NJS_VMCODE_3OPERANDS;
    prop_get->code.retval = NJS_VMCODE_RETVAL;
    prop_get->value = index;
    prop_get->object = lvalue->left->index;
    prop_get->property = lvalue->right->index;

    njs_generate_code(parser, njs_vmcode_3addr_t, code);
    code->code.operation = node->u.operation;
    code->code.operands = NJS_VMCODE_3OPERANDS;
    code->code.retval = NJS_VMCODE_RETVAL;
    code->dst = dest_index;
    code->src1 = index;
    code->src2 = index;

    njs_generate_code(parser, njs_vmcode_prop_set_t, prop_set);
    prop_set->code.operation = njs_vmcode_property_set;
    prop_set->code.operands = NJS_VMCODE_3OPERANDS;
    prop_set->code.retval = NJS_VMCODE_NO_RETVAL;
    prop_set->value = index;
    prop_set->object = lvalue->left->index;
    prop_set->property = lvalue->right->index;

    if (post) {
        ret = njs_generator_index_release(vm, parser, index);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    return njs_generator_children_indexes_release(vm, parser, lvalue);
}


static nxt_int_t
njs_generate_function_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t               ret;
    njs_value_t             *value;
    njs_function_t          *func;
    njs_parser_node_t       *body;
    njs_vmcode_operation_t  last;

    value = njs_variable_value(parser, node->index);
    func = value->data.u.function;

    body = node->right;

    if (body != NULL
        && body->right != NULL
        && body->right->token == NJS_TOKEN_RETURN)
    {
        last = NULL;

    } else {
        last = njs_vmcode_return;
    }

    ret = njs_generate_scope(vm, func->code.script->u.parser, body, last);

    if (nxt_fast_path(ret == NXT_OK)) {
        parser = func->code.script->u.parser;

        func->code.script->local_size = parser->scope_size;
        func->code.script->spare_size = parser->method_arguments_size;
        func->code.script->local_scope = parser->local_scope;
        func->code.script->u.code = parser->code_start;
        node->u.value = *value;
    }

    return ret;
}


nxt_int_t
njs_generate_scope(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node,
    njs_vmcode_operation_t last)
{
    size_t             code_size, size;
    u_char             *p;
    uintptr_t          scope_size;
    nxt_uint_t         n;
    njs_index_t        index;
    njs_value_t        *value;
    njs_vm_code_t      *code;
    njs_vmcode_stop_t  *stop;

    p = nxt_mem_cache_alloc(vm->mem_cache_pool, parser->code_size);
    if (nxt_slow_path(p == NULL)) {
        return NXT_ERROR;
    }

    parser->code_start = p;
    parser->code_end = p;

    if (node != NULL) {
        if (nxt_slow_path(njs_generator(vm, parser, node) != NXT_OK)) {
            return NXT_ERROR;
        }
    }

    if (last != NULL) {
        njs_generate_code(parser, njs_vmcode_stop_t, stop);
        stop->code.operation = last;
        stop->code.operands = NJS_VMCODE_1OPERAND;
        stop->code.retval = NJS_VMCODE_NO_RETVAL;

        index = njs_value_index(vm, parser, &njs_value_void);

        if (last == njs_vmcode_stop && node->index != 0) {
            index = node->index;
        }

        stop->retval = index;
    }

    code_size = parser->code_end - parser->code_start;

    nxt_thread_log_debug("SCOPE CODE SIZE: %uz %uz",
                         parser->code_size, code_size);

    if (nxt_slow_path(parser->code_size < code_size)) {
        return NXT_ERROR;
    }

    scope_size = parser->index[parser->scope - NJS_INDEX_CACHE]
                 - parser->scope_offset;

    parser->local_scope = nxt_mem_cache_alloc(vm->mem_cache_pool, scope_size);
    if (nxt_slow_path(parser->local_scope == NULL)) {
        return NXT_ERROR;
    }

    parser->scope_size = scope_size;

    size = parser->scope_values->items * sizeof(njs_value_t);

    nxt_thread_log_debug("SCOPE SIZE: %uz %uz", size, scope_size);

    p = memcpy(parser->local_scope, parser->scope_values->start, size);
    value = (njs_value_t *) (p + size);

    for (n = scope_size - size; n != 0; n -= sizeof(njs_value_t)) {
        *value++ = njs_value_void;
    }

    if (vm->code == NULL) {
        vm->code = nxt_array_create(4, sizeof(njs_vm_code_t),
                                    &njs_array_mem_proto, vm->mem_cache_pool);
        if (nxt_slow_path(vm->code == NULL)) {
            return NXT_ERROR;
        }
    }

    code = nxt_array_add(vm->code, &njs_array_mem_proto, vm->mem_cache_pool);
    if (nxt_slow_path(code == NULL)) {
        return NXT_ERROR;
    }

    code->start = parser->code_start;
    code->end = parser->code_end;

    return NXT_OK;
}


static nxt_int_t
njs_generate_return_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t          ret;
    njs_vmcode_stop_t  *code;

    ret = njs_generator(vm, parser, node->right);

    if (nxt_fast_path(ret == NXT_OK)) {
        njs_generate_code(parser, njs_vmcode_stop_t, code);
        code->code.operation = njs_vmcode_return;
        code->code.operands = NJS_VMCODE_1OPERAND;
        code->code.retval = NJS_VMCODE_NO_RETVAL;
        code->retval = node->right->index;
        node->index = node->right->index;
    }

    return ret;
}


static nxt_int_t
njs_generate_function_call(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    uintptr_t              nargs;
    nxt_int_t              ret;
    njs_index_t            retval, index, name;
    njs_parser_node_t      *arg;
    njs_vmcode_call_t      *call;
    njs_vmcode_move_t      *move;
    njs_vmcode_function_t  *func;

    if (node->left != NULL) {
        /* Generate function code in function expression. */
        ret = njs_generator(vm, parser, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        name = node->left->index;

    } else {
        /* njs_generate_variable() always returns NXT_OK. */
        (void) njs_generate_variable(parser, node);
        name = node->index;
    }

    njs_generate_code(parser, njs_vmcode_function_t, func);
    func->code.operation = njs_vmcode_function;
    func->code.operands = NJS_VMCODE_2OPERANDS;
    func->code.retval = NJS_VMCODE_RETVAL;
    func->code.ctor = node->ctor;
    func->name = name;

    index = njs_generator_temp_index_get(parser);
    func->function = index;

    nargs = 1;

    for (arg = node->right; arg != NULL; arg = arg->right) {
        nargs++;

        ret = njs_generator(vm, parser, arg->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        if (arg->index != arg->left->index) {
            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            move->dst = arg->index;
            move->src = arg->left->index;
        }
    }

    func->code.nargs = nargs;

    retval = njs_generator_dest_index(vm, parser, node);

    if (nxt_slow_path(retval == NJS_INDEX_ERROR)) {
        return retval;
    }

    if (retval == NJS_INDEX_NONE) {
        node->temporary = 1;
        retval = index;
    }

    node->index = retval;

    njs_generate_code(parser, njs_vmcode_call_t, call);
    call->code.operation = njs_vmcode_call;
    call->code.operands = NJS_VMCODE_2OPERANDS;
    call->code.retval = NJS_VMCODE_NO_RETVAL;
    call->code.nargs = nargs;
    call->function = index;
    call->retval = retval;

    if (retval == index) {
        return NXT_OK;
    }

    return njs_generator_index_release(vm, parser, index);
}


static nxt_int_t
njs_generate_method_call(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    uintptr_t            nargs;
    nxt_int_t            ret;
    njs_index_t          retval, index;
    njs_parser_node_t    *arg, *prop;
    njs_vmcode_call_t    *call;
    njs_vmcode_move_t    *move;
    njs_vmcode_method_t  *method;

    prop = node->left;

    /* Object. */

    ret = njs_generator(vm, parser, prop->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Method name. */

    ret = njs_generator(vm, parser, prop->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (prop->left->temporary) {
        index = prop->left->index;

        ret = njs_generator_node_index_release(vm, parser, prop->right);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

    } else if (prop->right->temporary) {
        index = prop->right->index;

    } else {
        index = njs_generator_temp_index_get(parser);
    }

    njs_generate_code(parser, njs_vmcode_method_t, method);
    method->code.operation = njs_vmcode_method;
    method->code.operands = NJS_VMCODE_3OPERANDS;
    method->code.retval = NJS_VMCODE_RETVAL;
    method->code.ctor = node->ctor;
    method->function = index;
    method->object = prop->left->index;
    method->method = prop->right->index;

    nargs = 1;

    for (arg = node->right; arg != NULL; arg = arg->right) {
        nargs++;

        ret = njs_generator(vm, parser, arg->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        if (arg->index != arg->left->index) {
            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            move->dst = arg->index;
            move->src = arg->left->index;
        }
    }

    method->code.nargs = nargs;

    retval = njs_generator_dest_index(vm, parser, node);

    if (nxt_slow_path(retval == NJS_INDEX_ERROR)) {
        return retval;
    }

    if (retval == NJS_INDEX_NONE) {
        node->temporary = 1;
        retval = index;
    }

    node->index = retval;

    njs_generate_code(parser, njs_vmcode_call_t, call);
    call->code.operation = njs_vmcode_call;
    call->code.operands = NJS_VMCODE_2OPERANDS;
    call->code.retval = NJS_VMCODE_NO_RETVAL;
    call->code.nargs = nargs;
    call->function = index;
    call->retval = retval;

    if (retval == index) {
        return NXT_OK;
    }

    return njs_generator_index_release(vm, parser, index);
}


static nxt_int_t
njs_generate_try_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t               ret;
    njs_index_t             index;
    njs_vmcode_catch_t      *catch;
    njs_vmcode_finally_t    *finally;
    njs_vmcode_try_end_t    *try_end, *catch_end;
    njs_vmcode_try_start_t  *try_start;

    njs_generate_code(parser, njs_vmcode_try_start_t, try_start);
    try_start->code.operation = njs_vmcode_try_start;
    try_start->code.operands = NJS_VMCODE_2OPERANDS;
    try_start->code.retval = NJS_VMCODE_NO_RETVAL;
    index = njs_generator_temp_index_get(parser);
    try_start->value = index;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_try_end_t, try_end);
    try_end->code.operation = njs_vmcode_try_end;
    try_end->code.operands = NJS_VMCODE_NO_OPERAND;
    try_end->code.retval = NJS_VMCODE_NO_RETVAL;

    try_start->offset = parser->code_end - (u_char *) try_start;

    node = node->right;

    if (node->token == NJS_TOKEN_CATCH) {
        /* A try/catch case. */

        ret = njs_generator(vm, parser, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code(parser, njs_vmcode_catch_t, catch);
        catch->code.operation = njs_vmcode_catch;
        catch->code.operands = NJS_VMCODE_2OPERANDS;
        catch->code.retval = NJS_VMCODE_NO_RETVAL;
        catch->offset = sizeof(njs_vmcode_catch_t);
        catch->exception = node->left->index;

        ret = njs_generator(vm, parser, node->right);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        try_end->offset = parser->code_end - (u_char *) try_end;

        /* TODO: release exception variable index. */

    } else {
        if (node->left != NULL) {
            /* A try/catch/finally case. */

            ret = njs_generator(vm, parser, node->left->left);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            njs_generate_code(parser, njs_vmcode_catch_t, catch);
            catch->code.operation = njs_vmcode_catch;
            catch->code.operands = NJS_VMCODE_2OPERANDS;
            catch->code.retval = NJS_VMCODE_NO_RETVAL;
            catch->exception = node->left->left->index;

            ret = njs_generator(vm, parser, node->left->right);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            njs_generate_code(parser, njs_vmcode_try_end_t, catch_end);
            catch_end->code.operation = njs_vmcode_try_end;
            catch_end->code.operands = NJS_VMCODE_NO_OPERAND;
            catch_end->code.retval = NJS_VMCODE_NO_RETVAL;

            catch->offset = parser->code_end - (u_char *) catch;

            /* TODO: release exception variable index. */

            njs_generate_code(parser, njs_vmcode_catch_t, catch);
            catch->code.operation = njs_vmcode_catch;
            catch->code.operands = NJS_VMCODE_2OPERANDS;
            catch->code.retval = NJS_VMCODE_NO_RETVAL;
            catch->offset = sizeof(njs_vmcode_catch_t);
            catch->exception = index;

            catch_end->offset = parser->code_end - (u_char *) catch_end;

        } else {
            /* A try/finally case. */

            njs_generate_code(parser, njs_vmcode_catch_t, catch);
            catch->code.operation = njs_vmcode_catch;
            catch->code.operands = NJS_VMCODE_2OPERANDS;
            catch->code.retval = NJS_VMCODE_NO_RETVAL;
            catch->offset = sizeof(njs_vmcode_catch_t);
            catch->exception = index;
        }

        try_end->offset = parser->code_end - (u_char *) try_end;

        ret = njs_generator(vm, parser, node->right);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code(parser, njs_vmcode_finally_t, finally);
        finally->code.operation = njs_vmcode_finally;
        finally->code.operands = NJS_VMCODE_1OPERAND;
        finally->code.retval = NJS_VMCODE_NO_RETVAL;
        finally->retval = index;
    }

    return njs_generator_index_release(vm, parser, index);
}


static nxt_int_t
njs_generate_throw_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t           ret;
    njs_vmcode_throw_t  *code;

    ret = njs_generator(vm, parser, node->right);

    if (nxt_fast_path(ret == NXT_OK)) {
        njs_generate_code(parser, njs_vmcode_throw_t, code);
        code->code.operation = njs_vmcode_throw;
        code->code.operands = NJS_VMCODE_1OPERAND;
        code->code.retval = NJS_VMCODE_NO_RETVAL;
        code->retval = node->right->index;
        node->index = node->right->index;
    }

    return ret;
}


static njs_index_t
njs_generator_dest_index(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    njs_index_t        ret;
    njs_parser_node_t  *dest;

    dest = node->dest;

    if (dest == NULL) {
        return NJS_INDEX_NONE;
    }

    if (dest->token == NJS_TOKEN_PROPERTY) {
        ret = njs_generator_index_release(vm, parser, dest->index);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    if (node->left != NULL) {
        ret = njs_generator_node_index_release(vm, parser, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    if (node->right != NULL) {
        ret = njs_generator_node_index_release(vm, parser, node->right);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    dest->lvalue = NJS_LVALUE_ASSIGNED;

    return dest->index;
}


static nxt_noinline njs_index_t
njs_generator_temp_index_get(njs_parser_t *parser)
{
    nxt_uint_t   n;
    njs_index_t  index, *last;
    nxt_array_t  *cache;

    cache = parser->index_cache;

    if (cache != NULL && cache->items != 0) {
        last = nxt_array_remove_last(cache);

        nxt_thread_log_debug("CACHE %p", *last);

        return *last;
    }

    /* Skip absolute and propery scopes. */
    n = parser->scope - NJS_INDEX_CACHE;

    index = parser->index[n];
    parser->index[n] += sizeof(njs_value_t);

    index |= parser->scope;

    nxt_thread_log_debug("GET %p", index);

    return index;
}


static nxt_noinline nxt_int_t
njs_generator_children_indexes_release(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generator_node_index_release(vm, parser, node->left);
    if (nxt_fast_path(ret == NXT_OK)) {
        return njs_generator_node_index_release(vm, parser, node->right);
    }

    return ret;
}


static nxt_noinline nxt_int_t
njs_generator_node_index_release(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    if (node->temporary) {
        return njs_generator_index_release(vm, parser, node->index);
    }

    return NXT_OK;
}


static nxt_noinline nxt_int_t
njs_generator_index_release(njs_vm_t *vm, njs_parser_t *parser,
    njs_index_t index)
{
    njs_index_t  *last;
    nxt_array_t  *cache;

    nxt_thread_log_debug("RELEASE %p", index);

    cache = parser->index_cache;

    if (cache == NULL) {
        cache = nxt_array_create(4, sizeof(njs_value_t *), &njs_array_mem_proto,
                                 vm->mem_cache_pool);
        if (nxt_slow_path(cache == NULL)) {
            return NXT_ERROR;
        }

        parser->index_cache = cache;
    }

    last = nxt_array_add(cache, &njs_array_mem_proto, vm->mem_cache_pool);
    if (nxt_fast_path(last != NULL)) {
        *last = index;
        return NXT_OK;
    }

    return NXT_ERROR;
}
