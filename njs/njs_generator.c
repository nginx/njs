
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
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
static nxt_int_t njs_generate_name(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_builtin_object(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_variable(njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_if_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_cond_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_switch_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_while_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_do_while_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_for_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_for_in_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_noinline nxt_int_t njs_generate_start_block(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_block_type_t type, const nxt_str_t *label);
static nxt_noinline void njs_generate_patch_loop_continuation(njs_vm_t *vm,
    njs_parser_t *parser);
static nxt_noinline void njs_generate_patch_block_exit(njs_vm_t *vm,
    njs_parser_t *parser);
static nxt_int_t njs_generate_continue_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_break_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_children(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_stop_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_comma_expression(njs_vm_t *vm,
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
static nxt_int_t njs_generate_test_jump_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_3addr_operation(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_2addr_operation(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_inc_dec_operation(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node, nxt_bool_t post);
static nxt_int_t njs_generate_function_declaration(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_function_scope(njs_vm_t *vm,
    njs_function_lambda_t *lambda, njs_parser_node_t *node);
static nxt_int_t njs_generate_return_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_function_call(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_method_call(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_noinline nxt_int_t njs_generate_call(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_int_t njs_generate_try_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static nxt_int_t njs_generate_throw_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_noinline njs_index_t njs_generator_dest_index(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *node);
static nxt_noinline njs_index_t
    njs_generator_object_dest_index(njs_parser_t *parser,
    njs_parser_node_t *node);
static njs_index_t njs_generator_node_temp_index_get(njs_parser_t *parser,
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
nxt_inline nxt_bool_t njs_generator_is_constant(njs_parser_node_t *node);


static const nxt_str_t  no_label = { 0, NULL };


static nxt_int_t
njs_generator(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    njs_parser_node_t  *left;

    if (node == NULL) {
        return NXT_OK;
    }

    switch (node->token) {

    case NJS_TOKEN_IF:
        return njs_generate_if_statement(vm, parser, node);

    case NJS_TOKEN_CONDITIONAL:
        return njs_generate_cond_expression(vm, parser, node);

    case NJS_TOKEN_SWITCH:
        return njs_generate_switch_statement(vm, parser, node);

    case NJS_TOKEN_WHILE:
        return njs_generate_while_statement(vm, parser, node);

    case NJS_TOKEN_DO:
        return njs_generate_do_while_statement(vm, parser, node);

    case NJS_TOKEN_FOR:
        return njs_generate_for_statement(vm, parser, node);

    case NJS_TOKEN_FOR_IN:
        return njs_generate_for_in_statement(vm, parser, node);

    case NJS_TOKEN_CONTINUE:
        return njs_generate_continue_statement(vm, parser, node);

    case NJS_TOKEN_BREAK:
        return njs_generate_break_statement(vm, parser, node);

    case NJS_TOKEN_STATEMENT:
        return njs_generate_statement(vm, parser, node);

    case NJS_TOKEN_END:
        return njs_generate_stop_statement(vm, parser, node);

    case NJS_TOKEN_CALL:
        return njs_generate_children(vm, parser, node);

    case NJS_TOKEN_COMMA:
        return njs_generate_comma_expression(vm, parser, node);

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

    case NJS_TOKEN_LOGICAL_AND:
    case NJS_TOKEN_LOGICAL_OR:
        return njs_generate_test_jump_expression(vm, parser, node);

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

    case NJS_TOKEN_OBJECT_VALUE:
        node->index = node->u.object->index;
        return NXT_OK;

    case NJS_TOKEN_OBJECT:
        return njs_generate_object(vm, parser, node);

    case NJS_TOKEN_ARRAY:
        return njs_generate_array(vm, parser, node);

    case NJS_TOKEN_FUNCTION_EXPRESSION:
        return njs_generate_function(vm, parser, node);

    case NJS_TOKEN_REGEXP:
        return njs_generate_regexp(vm, parser, node);

    case NJS_TOKEN_THIS:
    case NJS_TOKEN_OBJECT_CONSTRUCTOR:
    case NJS_TOKEN_ARRAY_CONSTRUCTOR:
    case NJS_TOKEN_NUMBER_CONSTRUCTOR:
    case NJS_TOKEN_BOOLEAN_CONSTRUCTOR:
    case NJS_TOKEN_STRING_CONSTRUCTOR:
    case NJS_TOKEN_FUNCTION_CONSTRUCTOR:
    case NJS_TOKEN_REGEXP_CONSTRUCTOR:
    case NJS_TOKEN_DATE_CONSTRUCTOR:
    case NJS_TOKEN_EVAL:
    case NJS_TOKEN_EXTERNAL:
        return NXT_OK;

    case NJS_TOKEN_NAME:
        return njs_generate_name(vm, parser, node);

    case NJS_TOKEN_MATH:
        return njs_generate_builtin_object(vm, parser, node);

    case NJS_TOKEN_FUNCTION:
        return njs_generate_function_declaration(vm, parser, node);

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
njs_generate_name(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    njs_index_t               index;
    njs_value_t               *value;
    njs_vmcode_object_copy_t  *copy;

    index = node->u.variable->index;
    value = njs_variable_value(parser, index);

    if (value->type == NJS_FUNCTION) {

        node->index = njs_generator_dest_index(vm, parser, node);
        if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
            return node->index;
        }

        njs_generate_code(parser, njs_vmcode_object_copy_t, copy);
        copy->code.operation = njs_vmcode_object_copy;
        copy->code.operands = NJS_VMCODE_2OPERANDS;
        copy->code.retval = NJS_VMCODE_RETVAL;
        copy->retval = node->index;
        copy->object = index;

        return NXT_OK;
    }

    return njs_generate_variable(parser, node);
}


static nxt_int_t
njs_generate_builtin_object(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    njs_index_t               index;
    njs_vmcode_object_copy_t  *copy;

    index = node->index;

    node->index = njs_generator_dest_index(vm, parser, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    njs_generate_code(parser, njs_vmcode_object_copy_t, copy);
    copy->code.operation = njs_vmcode_object_copy;
    copy->code.operands = NJS_VMCODE_2OPERANDS;
    copy->code.retval = NJS_VMCODE_RETVAL;
    copy->retval = node->index;
    copy->object = index;

    return NXT_OK;
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
    u_char                  *previous;
    nxt_int_t               ret;
    njs_ret_t               *label;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The condition expression. */

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_cond_jump_t, cond_jump);
    cond_jump->code.operation = njs_vmcode_if_false_jump;
    cond_jump->code.operands = NJS_VMCODE_2OPERANDS;
    cond_jump->code.retval = NJS_VMCODE_NO_RETVAL;
    cond_jump->cond = node->left->index;

    ret = njs_generator_node_index_release(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    previous = (u_char *) cond_jump;
    label = &cond_jump->offset;

    if (node->right != NULL && node->right->token == NJS_TOKEN_BRANCHING) {

        /* The "then" branch in a case of "if/then/else" statement. */

        node = node->right;

        ret = njs_generator(vm, parser, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        ret = njs_generator_node_index_release(vm, parser, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        njs_generate_code(parser, njs_vmcode_jump_t, jump);
        jump->code.operation = njs_vmcode_jump;
        jump->code.operands = NJS_VMCODE_NO_OPERAND;
        jump->code.retval = NJS_VMCODE_NO_RETVAL;

        *label = parser->code_end - previous;
        previous = (u_char *) jump;
        label = &jump->offset;
    }

    /*
     * The "then" branch in a case of "if/then" statement
     * or the "else" branch in a case of "if/then/else" statement.
     */

    ret = njs_generator(vm, parser, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generator_node_index_release(vm, parser, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    *label = parser->code_end - previous;

    return NXT_OK;
}


static nxt_int_t
njs_generate_cond_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t               ret;
    njs_parser_node_t       *branch;
    njs_vmcode_move_t       *move;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The condition expression. */

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_cond_jump_t, cond_jump);
    cond_jump->code.operation = njs_vmcode_if_false_jump;
    cond_jump->code.operands = NJS_VMCODE_2OPERANDS;
    cond_jump->code.retval = NJS_VMCODE_NO_RETVAL;
    cond_jump->cond = node->left->index;

    node->index = njs_generator_dest_index(vm, parser, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    branch = node->right;

    /* The "true" branch. */

    ret = njs_generator(vm, parser, branch->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /*
     * Branches usually uses node->index as destination, however,
     * if branch expression is a literal, variable or assignment,
     * then a MOVE operation is required.
     */

    if (node->index != branch->left->index) {
        njs_generate_code(parser, njs_vmcode_move_t, move);
        move->code.operation = njs_vmcode_move;
        move->code.operands = NJS_VMCODE_2OPERANDS;
        move->code.retval = NJS_VMCODE_RETVAL;
        move->dst = node->index;
        move->src = branch->left->index;
    }

    ret = njs_generator_node_index_release(vm, parser, branch->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_jump_t, jump);
    jump->code.operation = njs_vmcode_jump;
    jump->code.operands = NJS_VMCODE_NO_OPERAND;
    jump->code.retval = NJS_VMCODE_NO_RETVAL;

    cond_jump->offset = parser->code_end - (u_char *) cond_jump;

    /* The "false" branch. */

    ret = njs_generator(vm, parser, branch->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (node->index != branch->right->index) {
        njs_generate_code(parser, njs_vmcode_move_t, move);
        move->code.operation = njs_vmcode_move;
        move->code.operands = NJS_VMCODE_2OPERANDS;
        move->code.retval = NJS_VMCODE_RETVAL;
        move->dst = node->index;
        move->src = branch->right->index;
    }

    jump->offset = parser->code_end - (u_char *) jump;

    ret = njs_generator_node_index_release(vm, parser, branch->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return NXT_OK;
}


static nxt_int_t
njs_generate_switch_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *swtch)
{
    nxt_int_t                ret;
    njs_index_t              index;
    njs_parser_node_t        *node, *expr, *branch;
    njs_vmcode_move_t        *move;
    njs_vmcode_jump_t        *jump;
    njs_parser_patch_t       *patch, *next, *patches, **last;
    njs_vmcode_equal_jump_t  *equal;

    /* The "switch" expression. */

    expr = swtch->left;

    ret = njs_generator(vm, parser, expr);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    index = expr->index;

    if (!expr->temporary) {
        index = njs_generator_temp_index_get(parser);

        njs_generate_code(parser, njs_vmcode_move_t, move);
        move->code.operation = njs_vmcode_move;
        move->code.operands = NJS_VMCODE_2OPERANDS;
        move->code.retval = NJS_VMCODE_RETVAL;
        move->dst = index;
        move->src = expr->index;
    }

    ret = njs_generate_start_block(vm, parser, NJS_PARSER_SWITCH, &no_label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    last = &patches;

    for (branch = swtch->right; branch != NULL; branch = branch->left) {

        if (branch->token != NJS_TOKEN_DEFAULT) {

            /* The "case" expression. */

            node = branch->right;

            ret = njs_generator(vm, parser, node->left);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            njs_generate_code(parser, njs_vmcode_equal_jump_t, equal);
            equal->code.operation = njs_vmcode_if_equal_jump;
            equal->code.operands = NJS_VMCODE_3OPERANDS;
            equal->code.retval = NJS_VMCODE_NO_RETVAL;
            equal->offset = offsetof(njs_vmcode_equal_jump_t, offset);
            equal->value1 = index;
            equal->value2 = node->left->index;

            ret = njs_generator_node_index_release(vm, parser, node->left);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            patch = nxt_mem_cache_alloc(vm->mem_cache_pool,
                                        sizeof(njs_parser_patch_t));
            if (nxt_slow_path(patch == NULL)) {
                return NXT_ERROR;
            }

            patch->address = &equal->offset;

            *last = patch;
            last = &patch->next;
        }
    }

    /* Release either temporary index or temporary expr->index. */
    ret = njs_generator_index_release(vm, parser, index);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_jump_t, jump);
    jump->code.operation = njs_vmcode_jump;
    jump->code.operands = NJS_VMCODE_1OPERAND;
    jump->code.retval = NJS_VMCODE_NO_RETVAL;
    jump->offset = offsetof(njs_vmcode_jump_t, offset);

    patch = patches;

    for (branch = swtch->right; branch != NULL; branch = branch->left) {

        if (branch->token == NJS_TOKEN_DEFAULT) {
            jump->offset = parser->code_end - (u_char *) jump;
            jump = NULL;
            node = branch;

        } else {
            *patch->address += parser->code_end - (u_char *) patch->address;
            next = patch->next;

            nxt_mem_cache_free(vm->mem_cache_pool, patch);

            patch = next;
            node = branch->right;
        }

        /* The "case/default" statements. */

        ret = njs_generator(vm, parser, node->right);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }
    }

    if (jump != NULL) {
        /* A "switch" without default case. */
        jump->offset = parser->code_end - (u_char *) jump;
    }

    /* Patch "break" statements offsets. */
    njs_generate_patch_block_exit(vm, parser);

    return NXT_OK;
}


static nxt_int_t
njs_generate_while_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    u_char                  *loop;
    nxt_int_t               ret;
    njs_parser_node_t       *condition;
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

    ret = njs_generate_start_block(vm, parser, NJS_PARSER_LOOP, &no_label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    loop = parser->code_end;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop condition. */

    njs_generate_patch_loop_continuation(vm, parser);

    jump->offset = parser->code_end - (u_char *) jump;

    condition = node->right;

    ret = njs_generator(vm, parser, condition);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_cond_jump_t, cond_jump);
    cond_jump->code.operation = njs_vmcode_if_true_jump;
    cond_jump->code.operands = NJS_VMCODE_2OPERANDS;
    cond_jump->code.retval = NJS_VMCODE_NO_RETVAL;
    cond_jump->offset = loop - (u_char *) cond_jump;
    cond_jump->cond = condition->index;

    njs_generate_patch_block_exit(vm, parser);

    return njs_generator_node_index_release(vm, parser, condition);
}


static nxt_int_t
njs_generate_do_while_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    u_char                  *loop;
    nxt_int_t               ret;
    njs_parser_node_t       *condition;
    njs_vmcode_cond_jump_t  *cond_jump;

    /* The loop body. */

    ret = njs_generate_start_block(vm, parser, NJS_PARSER_LOOP, &no_label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    loop = parser->code_end;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop condition. */

    njs_generate_patch_loop_continuation(vm, parser);

    condition = node->right;

    ret = njs_generator(vm, parser, condition);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_cond_jump_t, cond_jump);
    cond_jump->code.operation = njs_vmcode_if_true_jump;
    cond_jump->code.operands = NJS_VMCODE_2OPERANDS;
    cond_jump->code.retval = NJS_VMCODE_NO_RETVAL;
    cond_jump->offset = loop - (u_char *) cond_jump;
    cond_jump->cond = condition->index;

    njs_generate_patch_block_exit(vm, parser);

    return njs_generator_node_index_release(vm, parser, condition);
}


static nxt_int_t
njs_generate_for_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    u_char                  *loop;
    nxt_int_t               ret;
    njs_parser_node_t       *condition, *update;
    njs_vmcode_jump_t       *jump;
    njs_vmcode_cond_jump_t  *cond_jump;

    ret = njs_generate_start_block(vm, parser, NJS_PARSER_LOOP, &no_label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    jump = NULL;

    /* The loop initialization. */

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generator_node_index_release(vm, parser, node->left);
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

    njs_generate_patch_loop_continuation(vm, parser);

    update = node->right;

    ret = njs_generator(vm, parser, update);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generator_node_index_release(vm, parser, update);
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
        cond_jump->offset = loop - (u_char *) cond_jump;
        cond_jump->cond = condition->index;

        njs_generate_patch_block_exit(vm, parser);

        return njs_generator_node_index_release(vm, parser, condition);
    }

    njs_generate_code(parser, njs_vmcode_jump_t, jump);
    jump->code.operation = njs_vmcode_jump;
    jump->code.operands = NJS_VMCODE_NO_OPERAND;
    jump->code.retval = NJS_VMCODE_NO_RETVAL;
    jump->offset = loop - (u_char *) jump;

    njs_generate_patch_block_exit(vm, parser);

    return NXT_OK;
}


static nxt_int_t
njs_generate_for_in_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    u_char                     *loop;
    nxt_int_t                  ret;
    njs_index_t                index;
    njs_parser_node_t          *foreach;
    njs_vmcode_prop_next_t     *prop_next;
    njs_vmcode_prop_foreach_t  *prop_foreach;

    ret = njs_generate_start_block(vm, parser, NJS_PARSER_LOOP, &no_label);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The object. */

    foreach = node->left;

    ret = njs_generator(vm, parser, foreach->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_prop_foreach_t, prop_foreach);
    prop_foreach->code.operation = njs_vmcode_property_foreach;
    prop_foreach->code.operands = NJS_VMCODE_2OPERANDS;
    prop_foreach->code.retval = NJS_VMCODE_RETVAL;
    prop_foreach->object = foreach->right->index;

    index = njs_generator_temp_index_get(parser);
    prop_foreach->next = index;

    /* The loop body. */

    loop = parser->code_end;

    ret = njs_generator(vm, parser, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* The loop iterator. */

    njs_generate_patch_loop_continuation(vm, parser);

    prop_foreach->offset = parser->code_end - (u_char *) prop_foreach;

    ret = njs_generator(vm, parser, node->left->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_prop_next_t, prop_next);
    prop_next->code.operation = njs_vmcode_property_next;
    prop_next->code.operands = NJS_VMCODE_3OPERANDS;
    prop_next->code.retval = NJS_VMCODE_RETVAL;
    prop_next->retval = foreach->left->index;
    prop_next->object = foreach->right->index;
    prop_next->next = index;
    prop_next->offset = loop - (u_char *) prop_next;

    njs_generate_patch_block_exit(vm, parser);

    /*
     * Release object and iterator indexes: an object can be a function result
     * or a property of another object and an iterator can be given with "let".
     */
    ret = njs_generator_children_indexes_release(vm, parser, foreach);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return njs_generator_index_release(vm, parser, index);
}


static nxt_noinline nxt_int_t
njs_generate_start_block(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_block_type_t type, const nxt_str_t *label)
{
    njs_parser_block_t  *block;

    block = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_parser_block_t));

    if (nxt_fast_path(block != NULL)) {
        block->next = parser->block;
        parser->block = block;

        block->type = type;
        block->label = *label;
        block->continuation = NULL;
        block->exit = NULL;

        return NXT_OK;
    }

    return NXT_ERROR;
}


static nxt_noinline void
njs_generate_patch_loop_continuation(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_parser_block_t  *block;
    njs_parser_patch_t  *patch, *next;

    block = parser->block;

    for (patch = block->continuation; patch != NULL; patch = next) {
        *patch->address += parser->code_end - (u_char *) patch->address;
        next = patch->next;

        nxt_mem_cache_free(vm->mem_cache_pool, patch);
    }
}


static nxt_noinline void
njs_generate_patch_block_exit(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_parser_block_t  *block;
    njs_parser_patch_t  *patch, *next;

    block = parser->block;
    parser->block = block->next;

    for (patch = block->exit; patch != NULL; patch = next) {
        *patch->address += parser->code_end - (u_char *) patch->address;
        next = patch->next;

        nxt_mem_cache_free(vm->mem_cache_pool, patch);
    }

    nxt_mem_cache_free(vm->mem_cache_pool, block);
}


static nxt_int_t
njs_generate_continue_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    njs_vmcode_jump_t   *jump;
    njs_parser_patch_t  *patch;

    if (parser->block == NULL) {
        vm->exception = &njs_exception_syntax_error;
        return NXT_ERROR;
    }

    /* TODO: LABEL */

    if (parser->block->type != NJS_PARSER_LOOP) {
        vm->exception = &njs_exception_syntax_error;
        return NXT_ERROR;
    }

    patch = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_parser_patch_t));

    if (nxt_fast_path(patch != NULL)) {
        patch->next = parser->block->continuation;
        parser->block->continuation = patch;

        njs_generate_code(parser, njs_vmcode_jump_t, jump);
        jump->code.operation = njs_vmcode_jump;
        jump->code.operands = NJS_VMCODE_NO_OPERAND;
        jump->code.retval = NJS_VMCODE_NO_RETVAL;
        jump->offset = offsetof(njs_vmcode_jump_t, offset);

        patch->address = &jump->offset;
    }

    return NXT_OK;
}


static nxt_int_t
njs_generate_break_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    njs_vmcode_jump_t   *jump;
    njs_parser_patch_t  *patch;

    if (parser->block == NULL) {
        vm->exception = &njs_exception_syntax_error;
        return NXT_ERROR;
    }

    /* TODO: LABEL: loop and switch may have label, block must have label. */

    patch = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_parser_patch_t));

    if (nxt_fast_path(patch != NULL)) {
        patch->next = parser->block->exit;
        parser->block->exit = patch;

        njs_generate_code(parser, njs_vmcode_jump_t, jump);
        jump->code.operation = njs_vmcode_jump;
        jump->code.operands = NJS_VMCODE_NO_OPERAND;
        jump->code.retval = NJS_VMCODE_NO_RETVAL;
        jump->offset = offsetof(njs_vmcode_jump_t, offset);

        patch->address = &jump->offset;
    }

    return NXT_OK;
}


static nxt_int_t
njs_generate_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generate_children(vm, parser, node);

    if (nxt_fast_path(ret == NXT_OK)) {
        return njs_generator_node_index_release(vm, parser, node->right);
    }

    return ret;
}


static nxt_int_t
njs_generate_children(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generator_node_index_release(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return njs_generator(vm, parser, node->right);
}


static nxt_int_t
njs_generate_stop_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t          ret;
    njs_index_t        index;
    njs_vmcode_stop_t  *stop;

    ret = njs_generate_children(vm, parser, node);

    if (nxt_fast_path(ret == NXT_OK)) {
        njs_generate_code(parser, njs_vmcode_stop_t, stop);
        stop->code.operation = njs_vmcode_stop;
        stop->code.operands = NJS_VMCODE_1OPERAND;
        stop->code.retval = NJS_VMCODE_NO_RETVAL;

        index = NJS_INDEX_NONE;

        if (node->right != NULL) {
            index = node->right->index;
        }

        if (index == NJS_INDEX_NONE) {
            index = njs_value_index(vm, parser, &njs_value_void);
        }

        stop->retval = index;
    }

    return ret;
}


static nxt_int_t
njs_generate_comma_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generate_children(vm, parser, node);

    if (nxt_fast_path(ret == NXT_OK)) {
        node->index = node->right->index;
    }

    return ret;
}


static nxt_int_t
njs_generate_assignment(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t                   ret;
    njs_value_t                 *value;
    njs_parser_node_t           *lvalue, *expr, *object, *property;
    njs_vmcode_move_t           *move;
    njs_vmcode_prop_set_t       *prop_set;

    lvalue = node->left;
    expr = node->right;
    expr->dest = NULL;

    if (lvalue->token == NJS_TOKEN_NAME) {

        lvalue->index = lvalue->u.variable->index;

        /* Use a constant value is stored as variable initial value. */

        if (njs_generator_is_constant(expr)) {

            ret = njs_generator(vm, parser, expr);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }

            if (lvalue->state == NJS_VARIABLE_FIRST_ASSIGNMENT) {
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
        node->temporary = expr->temporary;

        return NXT_OK;
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    object = lvalue->left;

    ret = njs_generator(vm, parser, object);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Property. */

    property = lvalue->right;

    ret = njs_generator(vm, parser, property);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    if (nxt_slow_path(njs_parser_has_side_effect(expr))) {
        /*
         * Preserve object and property values stored in variables in a case
         * if the variables can be changed by side effects in expression.
         */
        if (object->token == NJS_TOKEN_NAME) {
            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            move->src = object->index;
            move->dst = njs_generator_node_temp_index_get(parser, object);
        }

        if (property->token == NJS_TOKEN_NAME) {
            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            move->src = property->index;
            move->dst = njs_generator_node_temp_index_get(parser, property);
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
    prop_set->value = expr->index;
    prop_set->object = object->index;
    prop_set->property = property->index;

    node->index = expr->index;
    node->temporary = expr->temporary;

    return njs_generator_children_indexes_release(vm, parser, lvalue);
}


static nxt_int_t
njs_generate_operation_assignment(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t              ret;
    njs_index_t            index;
    njs_parser_node_t      *lvalue, *expr, *object, *property;
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
        expr = node->right;

        if (nxt_slow_path(njs_parser_has_side_effect(expr))) {
            /* Preserve variable value if it may be changed by expression. */

            njs_generate_code(parser, njs_vmcode_move_t, move);
            move->code.operation = njs_vmcode_move;
            move->code.operands = NJS_VMCODE_2OPERANDS;
            move->code.retval = NJS_VMCODE_RETVAL;
            move->src = lvalue->index;

            index = njs_generator_temp_index_get(parser);
            move->dst = index;
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

        node->index = lvalue->index;

        if (lvalue->index != index) {
            ret = njs_generator_index_release(vm, parser, index);
            if (nxt_slow_path(ret != NXT_OK)) {
                return ret;
            }
        }

        return njs_generator_node_index_release(vm, parser, expr);
    }

    /* lvalue->token == NJS_TOKEN_PROPERTY */

    /* Object. */

    object = lvalue->left;

    ret = njs_generator(vm, parser, object);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /* Property. */

    property = lvalue->right;

    ret = njs_generator(vm, parser, property);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_prop_get_t, prop_get);
    prop_get->code.operation = njs_vmcode_property_get;
    prop_get->code.operands = NJS_VMCODE_3OPERANDS;
    prop_get->code.retval = NJS_VMCODE_RETVAL;
    prop_get->value = njs_generator_node_temp_index_get(parser, node);
    prop_get->object = object->index;
    prop_get->property = property->index;

    expr = node->right;

    ret = njs_generator(vm, parser, expr);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_3addr_t, code);
    code->code.operation = node->u.operation;
    code->code.operands = NJS_VMCODE_3OPERANDS;
    code->code.retval = NJS_VMCODE_RETVAL;
    code->dst = node->index;
    code->src1 = node->index;
    code->src2 = expr->index;

    njs_generate_code(parser, njs_vmcode_prop_set_t, prop_set);
    prop_set->code.operation = njs_vmcode_property_set;
    prop_set->code.operands = NJS_VMCODE_3OPERANDS;
    prop_set->code.retval = NJS_VMCODE_NO_RETVAL;
    prop_set->value = node->index;
    prop_set->object = object->index;
    prop_set->property = property->index;

    ret = njs_generator_children_indexes_release(vm, parser, lvalue);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    return njs_generator_node_index_release(vm, parser, expr);
}


static nxt_int_t
njs_generate_object(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    njs_vmcode_object_t  *object;

    node->index = njs_generator_object_dest_index(parser, node);

    njs_generate_code(parser, njs_vmcode_object_t, object);
    object->code.operation = njs_vmcode_object;
    object->code.operands = NJS_VMCODE_1OPERAND;
    object->code.retval = NJS_VMCODE_RETVAL;
    object->retval = node->index;

    if (node->left == NULL) {
        return NXT_OK;
    }

    /* Initialize object. */

    return njs_generator(vm, parser, node->left);
}


static nxt_int_t
njs_generate_array(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    njs_vmcode_array_t  *array;

    node->index = njs_generator_object_dest_index(parser, node);

    njs_generate_code(parser, njs_vmcode_array_t, array);
    array->code.operation = njs_vmcode_array;
    array->code.operands = NJS_VMCODE_1OPERAND;
    array->code.retval = NJS_VMCODE_RETVAL;
    array->retval = node->index;
    array->length = node->u.length;

    if (node->left == NULL) {
        return NXT_OK;
    }

    /* Initialize array. */

    return njs_generator(vm, parser, node->left);
}


static nxt_int_t
njs_generate_function(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t              ret;
    njs_function_lambda_t  *lambda;
    njs_vmcode_function_t  *function;

    lambda = node->u.value.data.u.lambda;

    ret = njs_generate_function_scope(vm, lambda, node);

    if (nxt_fast_path(ret == NXT_OK)) {
        njs_generate_code(parser, njs_vmcode_function_t, function);
        function->code.operation = njs_vmcode_function;
        function->code.operands = NJS_VMCODE_1OPERAND;
        function->code.retval = NJS_VMCODE_RETVAL;
        function->lambda = lambda;

        node->index = njs_generator_object_dest_index(parser, node);
        function->retval = node->index;
    }

    return ret;
}


static nxt_int_t
njs_generate_regexp(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    njs_vmcode_regexp_t  *regexp;

    node->index = njs_generator_object_dest_index(parser, node);

    njs_generate_code(parser, njs_vmcode_regexp_t, regexp);
    regexp->code.operation = njs_vmcode_regexp;
    regexp->code.operands = NJS_VMCODE_1OPERAND;
    regexp->code.retval = NJS_VMCODE_RETVAL;
    regexp->retval = node->index;
    regexp->pattern = node->u.value.data.u.data;

    return NXT_OK;
}


static nxt_int_t
njs_generate_delete(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    double              n;
    nxt_int_t           ret;
    njs_index_t         index;
    njs_parser_node_t   *operand;
    njs_vmcode_2addr_t  *delete;

    operand = node->left;

    /*
     * The "delete" operator returns "false" for "undefined", "NaN", and
     * "Infinity" constants but not for values, expressions and "-Infinity".
     */

    switch (operand->token) {

    case NJS_TOKEN_NAME:
        if (operand->u.variable->state == NJS_VARIABLE_DECLARED) {
            index = njs_value_index(vm, parser, &njs_value_false);
            goto done;
        }

        /* A property of the global object. */

        njs_generate_code(parser, njs_vmcode_2addr_t, delete);
        delete->code.operation = njs_vmcode_delete;
        delete->code.operands = NJS_VMCODE_2OPERANDS;
        delete->code.retval = NJS_VMCODE_RETVAL;
        delete->dst = njs_generator_node_temp_index_get(parser, node);
        delete->src = operand->u.variable->index;

        return NXT_OK;

    case NJS_TOKEN_NUMBER:
        n = operand->u.value.data.u.number;

        if (!njs_is_nan(n) && !(njs_is_infinity(n) && n > 0.0)) {
            break;
        }

        /* Fall through. */

    case NJS_TOKEN_UNDEFINED:
        index = njs_value_index(vm, parser, &njs_value_false);
        goto done;

    default:
        ret = njs_generator(vm, parser, operand);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        ret = njs_generator_node_index_release(vm, parser, operand);
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
njs_generate_test_jump_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t               ret;
    njs_vmcode_move_t       *move;
    njs_vmcode_test_jump_t  *test_jump;

    ret = njs_generator(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    njs_generate_code(parser, njs_vmcode_test_jump_t, test_jump);
    test_jump->code.operation = node->u.operation;
    test_jump->code.operands = NJS_VMCODE_2OPERANDS;
    test_jump->code.retval = NJS_VMCODE_RETVAL;
    test_jump->value = node->left->index;

    ret = njs_generator_node_index_release(vm, parser, node->left);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    node->index = njs_generator_dest_index(vm, parser, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    test_jump->retval = node->index;

    ret = njs_generator(vm, parser, node->right);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    /*
     * The right expression usually uses node->index as destination,
     * however, if the expression is a literal, variable or assignment,
     * then a MOVE operation is required.
     */

    if (node->index != node->right->index) {
        njs_generate_code(parser, njs_vmcode_move_t, move);
        move->code.operation = njs_vmcode_move;
        move->code.operands = NJS_VMCODE_2OPERANDS;
        move->code.retval = NJS_VMCODE_RETVAL;
        move->dst = node->index;
        move->src = node->right->index;
    }

    test_jump->offset = parser->code_end - (u_char *) test_jump;

    return njs_generator_node_index_release(vm, parser, node->right);
}


static nxt_int_t
njs_generate_3addr_operation(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t           ret;
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
            move->dst = njs_generator_node_temp_index_get(parser, left);
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

    /*
     * The temporary index of MOVE destination
     * will be released here as index of node->left.
     */
    node->index = njs_generator_dest_index(vm, parser, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    code->dst = node->index;

    nxt_thread_log_debug("CODE3  %p, %p, %p",
                         code->dst, code->src1, code->src2);

    return NXT_OK;
}


static nxt_int_t
njs_generate_2addr_operation(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t           ret;
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

    node->index = njs_generator_dest_index(vm, parser, node);
    if (nxt_slow_path(node->index == NJS_INDEX_ERROR)) {
        return node->index;
    }

    code->dst = node->index;

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

    if (node->dest != NULL) {
        dest_index = node->dest->index;

        if (dest_index != lvalue->left->index
            && dest_index != lvalue->right->index)
        {
            node->index = dest_index;
            goto found;
        }
    }

    dest_index = njs_generator_node_temp_index_get(parser, node);

found:

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
njs_generate_function_declaration(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t    ret;
    njs_value_t  *value;

    value = njs_variable_value(parser, node->index);

    ret = njs_generate_function_scope(vm, value->data.u.function->u.lambda,
                                      node);

    if (nxt_fast_path(ret == NXT_OK)) {
        node->u.value = *value;
    }

    return ret;
}


static nxt_int_t
njs_generate_function_scope(njs_vm_t *vm, njs_function_lambda_t *lambda,
    njs_parser_node_t *node)
{
    nxt_int_t  ret;

    ret = njs_generate_scope(vm, lambda->u.parser, node->right);

    if (nxt_fast_path(ret == NXT_OK)) {
        lambda->local_size = lambda->u.parser->scope_size;
        lambda->local_scope = lambda->u.parser->local_scope;
        lambda->u.start = lambda->u.parser->code_start;
    }

    return ret;
}


nxt_int_t
njs_generate_scope(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    u_char         *p;
    size_t         code_size, size;
    uintptr_t      scope_size;
    nxt_uint_t     n;
    njs_value_t    *value;
    njs_vm_code_t  *code;

    p = nxt_mem_cache_alloc(vm->mem_cache_pool, parser->code_size);
    if (nxt_slow_path(p == NULL)) {
        return NXT_ERROR;
    }

    parser->code_start = p;
    parser->code_end = p;

    if (nxt_slow_path(njs_generator(vm, parser, node) != NXT_OK)) {
        return NXT_ERROR;
    }

    code_size = parser->code_end - parser->code_start;

    nxt_thread_log_debug("SCOPE CODE SIZE: %uz %uz",
                         parser->code_size, code_size);

    if (nxt_slow_path(parser->code_size < code_size)) {
        vm->exception = &njs_exception_internal_error;
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
    nxt_int_t            ret;
    njs_index_t          index;
    njs_vmcode_return_t  *code;

    ret = njs_generator(vm, parser, node->right);

    if (nxt_fast_path(ret == NXT_OK)) {
        njs_generate_code(parser, njs_vmcode_return_t, code);
        code->code.operation = njs_vmcode_return;
        code->code.operands = NJS_VMCODE_1OPERAND;
        code->code.retval = NJS_VMCODE_NO_RETVAL;

        if (node->right != NULL) {
            index = node->right->index;

        } else {
            index = njs_value_index(vm, parser, &njs_value_void);
        }

        code->retval = index;
        node->index = index;
    }

    return ret;
}


static nxt_int_t
njs_generate_function_call(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t                    ret;
    njs_parser_node_t            *name;
    njs_vmcode_function_frame_t  *func;

    if (node->left != NULL) {
        /* Generate function code in function expression. */
        ret = njs_generator(vm, parser, node->left);
        if (nxt_slow_path(ret != NXT_OK)) {
            return ret;
        }

        name = node->left;

    } else {
        /* njs_generate_variable() always returns NXT_OK. */
        (void) njs_generate_variable(parser, node);
        name = node;
    }

    njs_generate_code(parser, njs_vmcode_function_frame_t, func);
    func->code.operation = njs_vmcode_function_frame;
    func->code.operands = NJS_VMCODE_2OPERANDS;
    func->code.retval = NJS_VMCODE_NO_RETVAL;
    func->code.ctor = node->ctor;
    func->name = name->index;

    ret = njs_generator_node_index_release(vm, parser, name);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generate_call(vm, parser, node);

    if (nxt_fast_path(ret >= 0)) {
        func->nargs = ret;
        return NXT_OK;
    }

    return ret;
}


static nxt_int_t
njs_generate_method_call(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_int_t                  ret;
    njs_parser_node_t          *prop;
    njs_vmcode_method_frame_t  *method;

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

    njs_generate_code(parser, njs_vmcode_method_frame_t, method);
    method->code.operation = njs_vmcode_method_frame;
    method->code.operands = NJS_VMCODE_3OPERANDS;
    method->code.retval = NJS_VMCODE_NO_RETVAL;
    method->code.ctor = node->ctor;
    method->object = prop->left->index;
    method->method = prop->right->index;

    ret = njs_generator_children_indexes_release(vm, parser, prop);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    ret = njs_generate_call(vm, parser, node);

    if (nxt_fast_path(ret >= 0)) {
        method->nargs = ret;
        return NXT_OK;
    }

    return ret;
}


static nxt_noinline nxt_int_t
njs_generate_call(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node)
{
    nxt_int_t                   ret;
    nxt_uint_t                  nargs;
    njs_index_t                 retval;
    njs_parser_node_t           *arg;
    njs_vmcode_move_t           *move;
    njs_vmcode_function_call_t  *call;

    nargs = 0;

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

    retval = njs_generator_dest_index(vm, parser, node);
    if (nxt_slow_path(retval == NJS_INDEX_ERROR)) {
        return retval;
    }

    node->index = retval;

    njs_generate_code(parser, njs_vmcode_function_call_t, call);
    call->code.operation = njs_vmcode_function_call;
    call->code.operands = NJS_VMCODE_1OPERAND;
    call->code.retval = NJS_VMCODE_NO_RETVAL;
    call->retval = retval;

    return nargs;
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
        /* A "try/catch" case. */

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
    njs_vmcode_throw_t  *throw;

    ret = njs_generator(vm, parser, node->right);

    if (nxt_fast_path(ret == NXT_OK)) {
        njs_generate_code(parser, njs_vmcode_throw_t, throw);
        throw->code.operation = njs_vmcode_throw;
        throw->code.operands = NJS_VMCODE_1OPERAND;
        throw->code.retval = NJS_VMCODE_NO_RETVAL;

        node->index = node->right->index;
        throw->retval = node->index;
    }

    return ret;
}


static nxt_noinline njs_index_t
njs_generator_dest_index(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    njs_index_t        ret;
    njs_parser_node_t  *dest;

    ret = njs_generator_children_indexes_release(vm, parser, node);
    if (nxt_slow_path(ret != NXT_OK)) {
        return ret;
    }

    dest = node->dest;

    if (dest != NULL && dest->index != NJS_INDEX_NONE) {
        dest->lvalue = NJS_LVALUE_ASSIGNED;

        return dest->index;
    }

    return njs_generator_node_temp_index_get(parser, node);
}


static nxt_noinline njs_index_t
njs_generator_object_dest_index(njs_parser_t *parser, njs_parser_node_t *node)
{
    njs_index_t        index;
    njs_parser_node_t  *dest;

    dest = node->dest;

    if (dest != NULL && dest->index != NJS_INDEX_NONE) {
        index = dest->index;

        if (njs_is_callee_argument_index(index)) {
            /* Assgin object directly to a callee argument. */
            return index;
        }

        if (node->left == NULL) {
            /* Assign empty object directly to variable */
            dest->lvalue = NJS_LVALUE_ASSIGNED;
            return index;
        }
    }

    return njs_generator_node_temp_index_get(parser, node);
}


static njs_index_t
njs_generator_node_temp_index_get(njs_parser_t *parser, njs_parser_node_t *node)
{
    node->temporary = 1;

    node->index = njs_generator_temp_index_get(parser);

    return node->index;
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
    if (node != NULL && node->temporary) {
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
        cache = nxt_array_create(4, sizeof(njs_value_t *),
                                 &njs_array_mem_proto, vm->mem_cache_pool);
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


nxt_inline nxt_bool_t
njs_generator_is_constant(njs_parser_node_t *node)
{
    return (node->token >= NJS_TOKEN_FIRST_CONST
            && node->token <= NJS_TOKEN_LAST_CONST);
}
