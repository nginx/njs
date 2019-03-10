
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>


typedef struct {
    njs_token_t                    token;
    njs_vmcode_operation_t         operation;
} njs_parser_operation_t;


typedef struct njs_parser_expression_s  njs_parser_expression_t;

struct njs_parser_expression_s {
    njs_token_t                    (*next)(njs_vm_t *,
                                       njs_parser_t *,
                                       const njs_parser_expression_t *,
                                       njs_token_t);
    const njs_parser_expression_t  *expression;
    nxt_uint_t                     count;

#if (NXT_SUNC)
    /*
     * SunC supports C99 flexible array members but does not allow
     * static struct's initialization with arbitrary number of members.
     */
    njs_parser_operation_t         op[6];
#else
    njs_parser_operation_t         op[];
#endif
};


static njs_token_t njs_parser_any_expression(njs_vm_t *vm,
    njs_parser_t *parser, const njs_parser_expression_t *expr,
    njs_token_t token);
static njs_token_t njs_parser_conditional_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_binary_expression(njs_vm_t *vm,
    njs_parser_t *parser, const njs_parser_expression_t *expr,
    njs_token_t token);
static njs_token_t njs_parser_exponential_expression(njs_vm_t *vm,
    njs_parser_t *parser, const njs_parser_expression_t *expr,
    njs_token_t token);
static njs_token_t njs_parser_unary_expression(njs_vm_t *vm,
    njs_parser_t *parser, const njs_parser_expression_t *expr,
    njs_token_t token);
static njs_token_t njs_parser_inc_dec_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_post_inc_dec_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_call_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_new_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_property_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_property_brackets(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);


static const njs_parser_expression_t
    njs_parser_factor_expression =
{
    njs_parser_exponential_expression,
    NULL,
    3, {
        { NJS_TOKEN_MULTIPLICATION, njs_vmcode_multiplication },
        { NJS_TOKEN_DIVISION, njs_vmcode_division },
        { NJS_TOKEN_REMAINDER, njs_vmcode_remainder },
    }
};


static const njs_parser_expression_t
    njs_parser_addition_expression =
{
    njs_parser_binary_expression,
    &njs_parser_factor_expression,
    2, {
        { NJS_TOKEN_ADDITION, njs_vmcode_addition },
        { NJS_TOKEN_SUBSTRACTION, njs_vmcode_substraction },
    }
};


static const njs_parser_expression_t
    njs_parser_bitwise_shift_expression =
{
    njs_parser_binary_expression,
    &njs_parser_addition_expression,
    3, {
        { NJS_TOKEN_LEFT_SHIFT, njs_vmcode_left_shift },
        { NJS_TOKEN_RIGHT_SHIFT, njs_vmcode_right_shift },
        { NJS_TOKEN_UNSIGNED_RIGHT_SHIFT, njs_vmcode_unsigned_right_shift },
    }
};


static const njs_parser_expression_t
    njs_parser_relational_expression =
{
    njs_parser_binary_expression,
    &njs_parser_bitwise_shift_expression,
    6, {
        { NJS_TOKEN_LESS, njs_vmcode_less },
        { NJS_TOKEN_LESS_OR_EQUAL, njs_vmcode_less_or_equal },
        { NJS_TOKEN_GREATER, njs_vmcode_greater },
        { NJS_TOKEN_GREATER_OR_EQUAL, njs_vmcode_greater_or_equal },
        { NJS_TOKEN_IN, njs_vmcode_property_in },
        { NJS_TOKEN_INSTANCEOF, njs_vmcode_instance_of },
    }
};


static const njs_parser_expression_t
    njs_parser_equality_expression =
{
    njs_parser_binary_expression,
    &njs_parser_relational_expression,
    4, {
        { NJS_TOKEN_EQUAL, njs_vmcode_equal },
        { NJS_TOKEN_NOT_EQUAL, njs_vmcode_not_equal },
        { NJS_TOKEN_STRICT_EQUAL, njs_vmcode_strict_equal },
        { NJS_TOKEN_STRICT_NOT_EQUAL, njs_vmcode_strict_not_equal },
    }
};


static const njs_parser_expression_t
    njs_parser_bitwise_and_expression =
{
    njs_parser_binary_expression,
    &njs_parser_equality_expression,
    1, {
        { NJS_TOKEN_BITWISE_AND, njs_vmcode_bitwise_and },
    }
};


static const njs_parser_expression_t
    njs_parser_bitwise_xor_expression =
{
    njs_parser_binary_expression,
    &njs_parser_bitwise_and_expression,
    1, {
        { NJS_TOKEN_BITWISE_XOR, njs_vmcode_bitwise_xor },
    }
};


static const njs_parser_expression_t
    njs_parser_bitwise_or_expression =
{
    njs_parser_binary_expression,
    &njs_parser_bitwise_xor_expression,
    1, {
        { NJS_TOKEN_BITWISE_OR, njs_vmcode_bitwise_or },
    }
};


static const njs_parser_expression_t
    njs_parser_logical_and_expression =
{
    njs_parser_binary_expression,
    &njs_parser_bitwise_or_expression,
    1, {
        { NJS_TOKEN_LOGICAL_AND, njs_vmcode_test_if_false },
    }
};


static const njs_parser_expression_t
    njs_parser_logical_or_expression =
{
    njs_parser_binary_expression,
    &njs_parser_logical_and_expression,
    1, {
        { NJS_TOKEN_LOGICAL_OR, njs_vmcode_test_if_true },
    }
};


static const njs_parser_expression_t
    njs_parser_comma_expression =
{
    njs_parser_any_expression,
    NULL,
    1, {
        { NJS_TOKEN_COMMA, NULL },
    }
};


njs_token_t
njs_parser_expression(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    return njs_parser_binary_expression(vm, parser,
                                        &njs_parser_comma_expression, token);
}


njs_token_t
njs_parser_var_expression(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    token = njs_parser_assignment_expression(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    for ( ;; ) {
        switch (token) {

        case NJS_TOKEN_ASSIGNMENT:
            nxt_thread_log_debug("JS: =");
            operation = njs_vmcode_move;
            break;

        default:
            return token;
        }

        if (!njs_parser_is_lvalue(parser->node)) {
            njs_parser_ref_error(vm, parser,
                                 "Invalid left-hand side in assignment");
            return NJS_TOKEN_ILLEGAL;
        }

        node = njs_parser_node_new(vm, parser, token);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.operation = operation;
        node->left = parser->node;

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_var_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node->right = parser->node;
        parser->node = node;
    }
}


static njs_token_t
njs_parser_any_expression(njs_vm_t *vm, njs_parser_t *parser,
    const njs_parser_expression_t *expr, njs_token_t token)
{
    return njs_parser_assignment_expression(vm, parser, token);
}


njs_token_t
njs_parser_assignment_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    token = njs_parser_conditional_expression(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    for ( ;; ) {
        switch (token) {

        case NJS_TOKEN_ASSIGNMENT:
            nxt_thread_log_debug("JS: =");
            operation = njs_vmcode_move;
            break;

        case NJS_TOKEN_ADDITION_ASSIGNMENT:
            nxt_thread_log_debug("JS: +=");
            operation = njs_vmcode_addition;
            break;

        case NJS_TOKEN_SUBSTRACTION_ASSIGNMENT:
            nxt_thread_log_debug("JS: -=");
            operation = njs_vmcode_substraction;
            break;

        case NJS_TOKEN_MULTIPLICATION_ASSIGNMENT:
            nxt_thread_log_debug("JS: *=");
            operation = njs_vmcode_multiplication;
            break;

        case NJS_TOKEN_EXPONENTIATION_ASSIGNMENT:
            nxt_thread_log_debug("JS: **=");
            operation = njs_vmcode_exponentiation;
            break;

        case NJS_TOKEN_DIVISION_ASSIGNMENT:
            nxt_thread_log_debug("JS: /=");
            operation = njs_vmcode_division;
            break;

        case NJS_TOKEN_REMAINDER_ASSIGNMENT:
            nxt_thread_log_debug("JS: %=");
            operation = njs_vmcode_remainder;
            break;

        case NJS_TOKEN_LEFT_SHIFT_ASSIGNMENT:
            nxt_thread_log_debug("JS: <<=");
            operation = njs_vmcode_left_shift;
            break;

        case NJS_TOKEN_RIGHT_SHIFT_ASSIGNMENT:
            nxt_thread_log_debug("JS: >>=");
            operation = njs_vmcode_right_shift;
            break;

        case NJS_TOKEN_UNSIGNED_RIGHT_SHIFT_ASSIGNMENT:
            nxt_thread_log_debug("JS: >>=");
            operation = njs_vmcode_unsigned_right_shift;
            break;

        case NJS_TOKEN_BITWISE_AND_ASSIGNMENT:
            nxt_thread_log_debug("JS: &=");
            operation = njs_vmcode_bitwise_and;
            break;

        case NJS_TOKEN_BITWISE_XOR_ASSIGNMENT:
            nxt_thread_log_debug("JS: ^=");
            operation = njs_vmcode_bitwise_xor;
            break;

        case NJS_TOKEN_BITWISE_OR_ASSIGNMENT:
            nxt_thread_log_debug("JS: |=");
            operation = njs_vmcode_bitwise_or;
            break;

        default:
            return token;
        }

        if (!njs_parser_is_lvalue(parser->node)) {
            token = parser->node->token;

            if (token == NJS_TOKEN_ARGUMENTS || token ==  NJS_TOKEN_EVAL) {
                njs_parser_syntax_error(vm, parser, "Identifier \"%s\" "
                                      "is forbidden as left-hand in assignment",
                                       (token == NJS_TOKEN_EVAL) ? "eval"
                                                                 : "arguments");

            } else {
                njs_parser_ref_error(vm, parser,
                                     "Invalid left-hand side in assignment");
            }

            return NJS_TOKEN_ILLEGAL;
        }

        node = njs_parser_node_new(vm, parser, token);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.operation = operation;
        node->left = parser->node;

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_assignment_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node->right = parser->node;
        parser->node = node;
    }
}


njs_token_t
njs_parser_conditional_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_parser_node_t  *node, *cond;

    token = njs_parser_binary_expression(vm, parser,
                                         &njs_parser_logical_or_expression,
                                         token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    for ( ;; ) {
        if (token != NJS_TOKEN_CONDITIONAL) {
            return token;
        }

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        cond = njs_parser_node_new(vm, parser, NJS_TOKEN_CONDITIONAL);
        if (nxt_slow_path(cond == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        cond->left = parser->node;

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_BRANCHING);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        cond->right = node;

        token = njs_parser_assignment_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (nxt_slow_path(token != NJS_TOKEN_COLON)) {
            return NJS_TOKEN_ILLEGAL;
        }

        node->left = parser->node;
        node->left->dest = cond;

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_assignment_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node->right = parser->node;
        node->right->dest = cond;

        parser->node = cond;
    }
}


static njs_token_t
njs_parser_binary_expression(njs_vm_t *vm, njs_parser_t *parser,
    const njs_parser_expression_t *expr, njs_token_t token)
{
    nxt_int_t                     n;
    njs_parser_node_t             *node;
    const njs_parser_operation_t  *op;

    token = expr->next(vm, parser, expr->expression, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    for ( ;; ) {
        n = expr->count;
        op = expr->op;

        do {
            if (op->token == token) {
                goto found;
            }

            op++;
            n--;

        } while (n != 0);

        return token;

    found:

        node = njs_parser_node_new(vm, parser, token);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.operation = op->operation;
        node->left = parser->node;
        node->left->dest = node;

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = expr->next(vm, parser, expr->expression, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node->right = parser->node;
        node->right->dest = node;
        parser->node = node;
    }
}


static njs_token_t
njs_parser_exponential_expression(njs_vm_t *vm, njs_parser_t *parser,
    const njs_parser_expression_t *expr, njs_token_t token)
{
    njs_parser_node_t  *node;

    token = njs_parser_unary_expression(vm, parser, NULL, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token == NJS_TOKEN_EXPONENTIATION) {

        node = njs_parser_node_new(vm, parser, token);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.operation = njs_vmcode_exponentiation;
        node->left = parser->node;
        node->left->dest = node;

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_exponential_expression(vm, parser, NULL, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node->right = parser->node;
        node->right->dest = node;
        parser->node = node;
    }

    return token;
}


static njs_token_t
njs_parser_unary_expression(njs_vm_t *vm, njs_parser_t *parser,
    const njs_parser_expression_t *expr, njs_token_t token)
{
    double                  num;
    njs_token_t             next;
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    switch (token) {

    case NJS_TOKEN_ADDITION:
        token = NJS_TOKEN_UNARY_PLUS;
        operation = njs_vmcode_unary_plus;
        break;

    case NJS_TOKEN_SUBSTRACTION:
        token = NJS_TOKEN_UNARY_NEGATION;
        operation = njs_vmcode_unary_negation;
        break;

    case NJS_TOKEN_LOGICAL_NOT:
        operation = njs_vmcode_logical_not;
        break;

    case NJS_TOKEN_BITWISE_NOT:
        operation = njs_vmcode_bitwise_not;
        break;

    case NJS_TOKEN_TYPEOF:
        operation = njs_vmcode_typeof;
        break;

    case NJS_TOKEN_VOID:
        operation = njs_vmcode_void;
        break;

    case NJS_TOKEN_DELETE:
        operation = njs_vmcode_delete;
        break;

    default:
        return njs_parser_inc_dec_expression(vm, parser, token);
    }

    next = njs_parser_token(vm, parser);
    if (nxt_slow_path(next <= NJS_TOKEN_ILLEGAL)) {
        return next;
    }

    next = njs_parser_unary_expression(vm, parser, NULL, next);
    if (nxt_slow_path(next <= NJS_TOKEN_ILLEGAL)) {
        return next;
    }

    if (next == NJS_TOKEN_EXPONENTIATION) {
        njs_parser_syntax_error(vm, parser, "Either left-hand side or entire "
                                "exponentiation must be parenthesized");

        return NJS_TOKEN_ILLEGAL;
    }

    node = parser->node;

    if (token == NJS_TOKEN_UNARY_PLUS && node->token == NJS_TOKEN_NUMBER) {
        /* Skip the unary plus of number. */
        return next;
    }

    if (token == NJS_TOKEN_UNARY_NEGATION && node->token == NJS_TOKEN_NUMBER) {

        /* Optimization of common negative number. */
        num = -node->u.value.data.u.number;
        node->u.value.data.u.number = num;
        node->u.value.data.truth = njs_is_number_true(num);

        return next;
    }

    if (token == NJS_TOKEN_DELETE) {

        switch (node->token) {

        case NJS_TOKEN_PROPERTY:
            node->token = NJS_TOKEN_PROPERTY_DELETE;
            node->u.operation = njs_vmcode_property_delete;

            return next;

        case NJS_TOKEN_NAME:
        case NJS_TOKEN_UNDEFINED:
            njs_parser_syntax_error(vm, parser,
                                    "Delete of an unqualified identifier");

            return NJS_TOKEN_ILLEGAL;

        default:
            break;
        }
    }

    if (token == NJS_TOKEN_TYPEOF && node->token == NJS_TOKEN_NAME) {
        node->u.reference.type = NJS_TYPEOF;
    }

    node = njs_parser_node_new(vm, parser, token);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->u.operation = operation;
    node->left = parser->node;
    node->left->dest = node;
    parser->node = node;

    return next;
}


static njs_token_t
njs_parser_inc_dec_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_token_t             next;
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    switch (token) {

    case NJS_TOKEN_INCREMENT:
        operation = njs_vmcode_increment;
        break;

    case NJS_TOKEN_DECREMENT:
        operation = njs_vmcode_decrement;
        break;

    default:
        return njs_parser_post_inc_dec_expression(vm, parser, token);
    }

    next = njs_parser_token(vm, parser);
    if (nxt_slow_path(next <= NJS_TOKEN_ILLEGAL)) {
        return next;
    }

    next = njs_parser_call_expression(vm, parser, next);
    if (nxt_slow_path(next <= NJS_TOKEN_ILLEGAL)) {
        return next;
    }

    if (!njs_parser_is_lvalue(parser->node)) {
        njs_parser_ref_error(vm, parser,
                             "Invalid left-hand side in prefix operation");
        return NJS_TOKEN_ILLEGAL;
    }

    node = njs_parser_node_new(vm, parser, token);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->u.operation = operation;
    node->left = parser->node;
    parser->node = node;

    return next;
}


static njs_token_t
njs_parser_post_inc_dec_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    token = njs_parser_call_expression(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    switch (token) {

    case NJS_TOKEN_INCREMENT:
        token = NJS_TOKEN_POST_INCREMENT;
        operation = njs_vmcode_post_increment;
        break;

    case NJS_TOKEN_DECREMENT:
        token = NJS_TOKEN_POST_DECREMENT;
        operation = njs_vmcode_post_decrement;
        break;

    default:
        return token;
    }

    /* Automatic semicolon insertion. */

    if (parser->lexer->prev_token == NJS_TOKEN_LINE_END) {
        njs_lexer_rollback(parser->lexer);
        return NJS_TOKEN_SEMICOLON;
    }

    if (!njs_parser_is_lvalue(parser->node)) {
        njs_parser_ref_error(vm, parser,
                             "Invalid left-hand side in postfix operation");
        return NJS_TOKEN_ILLEGAL;
    }

    node = njs_parser_node_new(vm, parser, token);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->u.operation = operation;
    node->left = parser->node;
    parser->node = node;

    return njs_parser_token(vm, parser);
}


static njs_token_t
njs_parser_call_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_parser_node_t  *func, *node;

    if (token == NJS_TOKEN_NEW) {
        token = njs_parser_new_expression(vm, parser, token);

    } else {
        token = njs_parser_terminal(vm, parser, token);
    }

    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    for ( ;; ) {
        token = njs_parser_property_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node = parser->node;

        if (token != NJS_TOKEN_OPEN_PARENTHESIS) {
            return token;
        }

        switch (node->token) {

        case NJS_TOKEN_NAME:
            func = node;
            func->token = NJS_TOKEN_FUNCTION_CALL;
            func->scope = parser->scope;

            break;

        case NJS_TOKEN_PROPERTY:
            func = njs_parser_node_new(vm, parser, NJS_TOKEN_METHOD_CALL);
            if (nxt_slow_path(func == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            func->left = node;

            break;

        default:
            /*
             * NJS_TOKEN_METHOD_CALL,
             * NJS_TOKEN_FUNCTION_CALL,
             * NJS_TOKEN_FUNCTION_EXPRESSION,
             * NJS_TOKEN_OPEN_PARENTHESIS,
             * NJS_TOKEN_OBJECT_CONSTRUCTOR,
             * NJS_TOKEN_ARRAY_CONSTRUCTOR,
             * NJS_TOKEN_BOOLEAN_CONSTRUCTOR,
             * NJS_TOKEN_NUMBER_CONSTRUCTOR,
             * NJS_TOKEN_STRING_CONSTRUCTOR,
             * NJS_TOKEN_FUNCTION_CONSTRUCTOR,
             * NJS_TOKEN_REGEXP_CONSTRUCTOR,
             * NJS_TOKEN_EVAL.
             */
            func = njs_parser_node_new(vm, parser, NJS_TOKEN_FUNCTION_CALL);
            if (nxt_slow_path(func == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            func->left = node;

            break;
        }

        func->ctor = 0;

        token = njs_parser_arguments(vm, parser, func);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        parser->node = func;

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }
    }
}


static njs_token_t
njs_parser_new_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_parser_node_t  *func, *node;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token == NJS_TOKEN_NEW) {
        token = njs_parser_new_expression(vm, parser, token);

    } else {
        token = njs_parser_terminal(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_property_expression(vm, parser, token);
    }

    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = parser->node;

    switch (node->token) {

    case NJS_TOKEN_NAME:
        func = node;
        func->token = NJS_TOKEN_FUNCTION_CALL;

        break;

    case NJS_TOKEN_PROPERTY:
        func = njs_parser_node_new(vm, parser, NJS_TOKEN_METHOD_CALL);
        if (nxt_slow_path(func == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        func->left = node;

        break;

    default:
        /*
         * NJS_TOKEN_METHOD_CALL,
         * NJS_TOKEN_FUNCTION_CALL,
         * NJS_TOKEN_FUNCTION_EXPRESSION,
         * NJS_TOKEN_OPEN_PARENTHESIS,
         * NJS_TOKEN_OBJECT_CONSTRUCTOR,
         * NJS_TOKEN_ARRAY_CONSTRUCTOR,
         * NJS_TOKEN_BOOLEAN_CONSTRUCTOR,
         * NJS_TOKEN_NUMBER_CONSTRUCTOR,
         * NJS_TOKEN_STRING_CONSTRUCTOR,
         * NJS_TOKEN_FUNCTION_CONSTRUCTOR,
         * NJS_TOKEN_REGEXP_CONSTRUCTOR,
         * NJS_TOKEN_EVAL.
         */
        func = njs_parser_node_new(vm, parser, NJS_TOKEN_FUNCTION_CALL);
        if (nxt_slow_path(func == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        func->left = node;

        break;
    }

    func->ctor = 1;

    if (token != NJS_TOKEN_OPEN_PARENTHESIS) {
        parser->node = func;
        return token;
    }

    token = njs_parser_arguments(vm, parser, func);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    parser->node = func;

    return njs_parser_token(vm, parser);
}


static njs_token_t
njs_parser_property_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_parser_node_t  *node;

    for ( ;; ) {
        if (token != NJS_TOKEN_DOT
            && token != NJS_TOKEN_OPEN_BRACKET)
        {
            return token;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_PROPERTY);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.operation = njs_vmcode_property_get;
        node->left = parser->node;

        if (token == NJS_TOKEN_DOT) {

            token = njs_parser_property_token(vm, parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            if (token != NJS_TOKEN_NAME) {
                return NJS_TOKEN_ILLEGAL;
            }

            token = njs_parser_token(vm, parser);

        } else {
            token = njs_parser_token(vm, parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            token = njs_parser_property_brackets(vm, parser, token);
        }

        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node->right = parser->node;
        parser->node = node;
    }
}


static njs_token_t
njs_parser_property_brackets(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    token = njs_parser_expression(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (nxt_slow_path(token != NJS_TOKEN_CLOSE_BRACKET)) {
        return NJS_TOKEN_ERROR;
    }

    return njs_parser_token(vm, parser);
}


njs_token_t
njs_parser_arguments(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent)
{
    njs_token_t        token;
    njs_index_t        index;
    njs_parser_node_t  *node;

    index = NJS_SCOPE_CALLEE_ARGUMENTS;

    do {
        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_CLOSE_PARENTHESIS) {
            break;
        }

        token = njs_parser_assignment_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_ARGUMENT);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->index = index;
        index += sizeof(njs_value_t);

        node->left = parser->node;
        parser->node->dest = node;
        parent->right = node;
        parent = node;

    } while (token == NJS_TOKEN_COMMA);

    if (nxt_slow_path(token != NJS_TOKEN_CLOSE_PARENTHESIS)) {
        return NJS_TOKEN_ILLEGAL;
    }

    return token;
}
