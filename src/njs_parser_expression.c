
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


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
    njs_uint_t                     count;

#if (NJS_SUNC)
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
static njs_token_t njs_parser_call(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, uint8_t ctor);
static njs_token_t njs_parser_arguments(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent);


static const njs_parser_expression_t
    njs_parser_factor_expression =
{
    njs_parser_exponential_expression,
    NULL,
    3, {
        { NJS_TOKEN_MULTIPLICATION, NJS_VMCODE_MULTIPLICATION },
        { NJS_TOKEN_DIVISION, NJS_VMCODE_DIVISION },
        { NJS_TOKEN_REMAINDER, NJS_VMCODE_REMAINDER },
    }
};


static const njs_parser_expression_t
    njs_parser_addition_expression =
{
    njs_parser_binary_expression,
    &njs_parser_factor_expression,
    2, {
        { NJS_TOKEN_ADDITION, NJS_VMCODE_ADDITION },
        { NJS_TOKEN_SUBSTRACTION, NJS_VMCODE_SUBSTRACTION },
    }
};


static const njs_parser_expression_t
    njs_parser_bitwise_shift_expression =
{
    njs_parser_binary_expression,
    &njs_parser_addition_expression,
    3, {
        { NJS_TOKEN_LEFT_SHIFT, NJS_VMCODE_LEFT_SHIFT },
        { NJS_TOKEN_RIGHT_SHIFT, NJS_VMCODE_RIGHT_SHIFT },
        { NJS_TOKEN_UNSIGNED_RIGHT_SHIFT, NJS_VMCODE_UNSIGNED_RIGHT_SHIFT },
    }
};


static const njs_parser_expression_t
    njs_parser_relational_expression =
{
    njs_parser_binary_expression,
    &njs_parser_bitwise_shift_expression,
    6, {
        { NJS_TOKEN_LESS, NJS_VMCODE_LESS },
        { NJS_TOKEN_LESS_OR_EQUAL, NJS_VMCODE_LESS_OR_EQUAL },
        { NJS_TOKEN_GREATER, NJS_VMCODE_GREATER },
        { NJS_TOKEN_GREATER_OR_EQUAL, NJS_VMCODE_GREATER_OR_EQUAL },
        { NJS_TOKEN_IN, NJS_VMCODE_PROPERTY_IN },
        { NJS_TOKEN_INSTANCEOF, NJS_VMCODE_INSTANCE_OF },
    }
};


static const njs_parser_expression_t
    njs_parser_equality_expression =
{
    njs_parser_binary_expression,
    &njs_parser_relational_expression,
    4, {
        { NJS_TOKEN_EQUAL, NJS_VMCODE_EQUAL },
        { NJS_TOKEN_NOT_EQUAL, NJS_VMCODE_NOT_EQUAL },
        { NJS_TOKEN_STRICT_EQUAL, NJS_VMCODE_STRICT_EQUAL },
        { NJS_TOKEN_STRICT_NOT_EQUAL, NJS_VMCODE_STRICT_NOT_EQUAL },
    }
};


static const njs_parser_expression_t
    njs_parser_bitwise_and_expression =
{
    njs_parser_binary_expression,
    &njs_parser_equality_expression,
    1, {
        { NJS_TOKEN_BITWISE_AND, NJS_VMCODE_BITWISE_AND },
    }
};


static const njs_parser_expression_t
    njs_parser_bitwise_xor_expression =
{
    njs_parser_binary_expression,
    &njs_parser_bitwise_and_expression,
    1, {
        { NJS_TOKEN_BITWISE_XOR, NJS_VMCODE_BITWISE_XOR },
    }
};


static const njs_parser_expression_t
    njs_parser_bitwise_or_expression =
{
    njs_parser_binary_expression,
    &njs_parser_bitwise_xor_expression,
    1, {
        { NJS_TOKEN_BITWISE_OR, NJS_VMCODE_BITWISE_OR },
    }
};


static const njs_parser_expression_t
    njs_parser_logical_and_expression =
{
    njs_parser_binary_expression,
    &njs_parser_bitwise_or_expression,
    1, {
        { NJS_TOKEN_LOGICAL_AND, NJS_VMCODE_TEST_IF_FALSE },
    }
};


static const njs_parser_expression_t
    njs_parser_logical_or_expression =
{
    njs_parser_binary_expression,
    &njs_parser_logical_and_expression,
    1, {
        { NJS_TOKEN_LOGICAL_OR, NJS_VMCODE_TEST_IF_TRUE },
    }
};


static const njs_parser_expression_t
    njs_parser_comma_expression =
{
    njs_parser_any_expression,
    NULL,
    1, {
        { NJS_TOKEN_COMMA, NJS_VMCODE_NOP },
    }
};


njs_token_t
njs_parser_expression(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    return njs_parser_binary_expression(vm, parser,
                                        &njs_parser_comma_expression, token);
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

    njs_parser_enter(vm, parser);

    token = njs_parser_conditional_expression(vm, parser, token);

    njs_parser_leave(parser);

    if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    for ( ;; ) {
        switch (token) {

        case NJS_TOKEN_ASSIGNMENT:
            njs_thread_log_debug("JS: =");
            operation = NJS_VMCODE_MOVE;
            break;

        case NJS_TOKEN_ADDITION_ASSIGNMENT:
            njs_thread_log_debug("JS: +=");
            operation = NJS_VMCODE_ADDITION;
            break;

        case NJS_TOKEN_SUBSTRACTION_ASSIGNMENT:
            njs_thread_log_debug("JS: -=");
            operation = NJS_VMCODE_SUBSTRACTION;
            break;

        case NJS_TOKEN_MULTIPLICATION_ASSIGNMENT:
            njs_thread_log_debug("JS: *=");
            operation = NJS_VMCODE_MULTIPLICATION;
            break;

        case NJS_TOKEN_EXPONENTIATION_ASSIGNMENT:
            njs_thread_log_debug("JS: **=");
            operation = NJS_VMCODE_EXPONENTIATION;
            break;

        case NJS_TOKEN_DIVISION_ASSIGNMENT:
            njs_thread_log_debug("JS: /=");
            operation = NJS_VMCODE_DIVISION;
            break;

        case NJS_TOKEN_REMAINDER_ASSIGNMENT:
            njs_thread_log_debug("JS: %=");
            operation = NJS_VMCODE_REMAINDER;
            break;

        case NJS_TOKEN_LEFT_SHIFT_ASSIGNMENT:
            njs_thread_log_debug("JS: <<=");
            operation = NJS_VMCODE_LEFT_SHIFT;
            break;

        case NJS_TOKEN_RIGHT_SHIFT_ASSIGNMENT:
            njs_thread_log_debug("JS: >>=");
            operation = NJS_VMCODE_RIGHT_SHIFT;
            break;

        case NJS_TOKEN_UNSIGNED_RIGHT_SHIFT_ASSIGNMENT:
            njs_thread_log_debug("JS: >>=");
            operation = NJS_VMCODE_UNSIGNED_RIGHT_SHIFT;
            break;

        case NJS_TOKEN_BITWISE_AND_ASSIGNMENT:
            njs_thread_log_debug("JS: &=");
            operation = NJS_VMCODE_BITWISE_AND;
            break;

        case NJS_TOKEN_BITWISE_XOR_ASSIGNMENT:
            njs_thread_log_debug("JS: ^=");
            operation = NJS_VMCODE_BITWISE_XOR;
            break;

        case NJS_TOKEN_BITWISE_OR_ASSIGNMENT:
            njs_thread_log_debug("JS: |=");
            operation = NJS_VMCODE_BITWISE_OR;
            break;

        default:
            return token;
        }

        if (!njs_parser_is_lvalue(parser->node)) {
            token = parser->node->token;

            if (token == NJS_TOKEN_ARGUMENTS || token == NJS_TOKEN_EVAL) {
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
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.operation = operation;
        node->left = parser->node;

        token = njs_parser_token(vm, parser);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        njs_parser_enter(vm, parser);

        token = njs_parser_assignment_expression(vm, parser, token);

        njs_parser_leave(parser);

        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
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
    if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    for ( ;; ) {
        if (token != NJS_TOKEN_CONDITIONAL) {
            return token;
        }

        token = njs_parser_token(vm, parser);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        cond = njs_parser_node_new(vm, parser, NJS_TOKEN_CONDITIONAL);
        if (njs_slow_path(cond == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        cond->left = parser->node;

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_BRANCHING);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        cond->right = node;

        token = njs_parser_assignment_expression(vm, parser, token);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (njs_slow_path(token != NJS_TOKEN_COLON)) {
            return NJS_TOKEN_ILLEGAL;
        }

        node->left = parser->node;
        node->left->dest = cond;

        token = njs_parser_token(vm, parser);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_assignment_expression(vm, parser, token);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
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
    njs_int_t                     n;
    njs_parser_node_t             *node;
    const njs_parser_operation_t  *op;

    token = expr->next(vm, parser, expr->expression, token);
    if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
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
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.operation = op->operation;
        node->left = parser->node;
        node->left->dest = node;

        token = njs_parser_token(vm, parser);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = expr->next(vm, parser, expr->expression, token);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
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
    if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token == NJS_TOKEN_EXPONENTIATION) {

        node = njs_parser_node_new(vm, parser, token);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.operation = NJS_VMCODE_EXPONENTIATION;
        node->left = parser->node;
        node->left->dest = node;

        token = njs_parser_token(vm, parser);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        njs_parser_enter(vm, parser);

        token = njs_parser_exponential_expression(vm, parser, NULL, token);

        njs_parser_leave(parser);

        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
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
        operation = NJS_VMCODE_UNARY_PLUS;
        break;

    case NJS_TOKEN_SUBSTRACTION:
        token = NJS_TOKEN_UNARY_NEGATION;
        operation = NJS_VMCODE_UNARY_NEGATION;
        break;

    case NJS_TOKEN_LOGICAL_NOT:
        operation = NJS_VMCODE_LOGICAL_NOT;
        break;

    case NJS_TOKEN_BITWISE_NOT:
        operation = NJS_VMCODE_BITWISE_NOT;
        break;

    case NJS_TOKEN_TYPEOF:
        operation = NJS_VMCODE_TYPEOF;
        break;

    case NJS_TOKEN_VOID:
        operation = NJS_VMCODE_VOID;
        break;

    case NJS_TOKEN_DELETE:
        operation = NJS_VMCODE_DELETE;
        break;

    default:
        return njs_parser_inc_dec_expression(vm, parser, token);
    }

    next = njs_parser_token(vm, parser);
    if (njs_slow_path(next <= NJS_TOKEN_ILLEGAL)) {
        return next;
    }

    njs_parser_enter(vm, parser);

    next = njs_parser_unary_expression(vm, parser, NULL, next);

    njs_parser_leave(parser);

    if (njs_slow_path(next <= NJS_TOKEN_ILLEGAL)) {
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
        num = -njs_number(&node->u.value);
        njs_set_number(&node->u.value, num);

        return next;
    }

    if (token == NJS_TOKEN_DELETE) {

        switch (node->token) {

        case NJS_TOKEN_PROPERTY:
            node->token = NJS_TOKEN_PROPERTY_DELETE;
            node->u.operation = NJS_VMCODE_PROPERTY_DELETE;

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
    if (njs_slow_path(node == NULL)) {
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
        operation = NJS_VMCODE_INCREMENT;
        break;

    case NJS_TOKEN_DECREMENT:
        operation = NJS_VMCODE_DECREMENT;
        break;

    default:
        return njs_parser_post_inc_dec_expression(vm, parser, token);
    }

    next = njs_parser_token(vm, parser);
    if (njs_slow_path(next <= NJS_TOKEN_ILLEGAL)) {
        return next;
    }

    next = njs_parser_call_expression(vm, parser, next);
    if (njs_slow_path(next <= NJS_TOKEN_ILLEGAL)) {
        return next;
    }

    if (!njs_parser_is_lvalue(parser->node)) {
        njs_parser_ref_error(vm, parser,
                             "Invalid left-hand side in prefix operation");
        return NJS_TOKEN_ILLEGAL;
    }

    node = njs_parser_node_new(vm, parser, token);
    if (njs_slow_path(node == NULL)) {
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
    njs_int_t               ret;
    njs_parser_node_t       *node;
    njs_vmcode_operation_t  operation;

    token = njs_parser_call_expression(vm, parser, token);
    if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    switch (token) {

    case NJS_TOKEN_INCREMENT:
        token = NJS_TOKEN_POST_INCREMENT;
        operation = NJS_VMCODE_POST_INCREMENT;
        break;

    case NJS_TOKEN_DECREMENT:
        token = NJS_TOKEN_POST_DECREMENT;
        operation = NJS_VMCODE_POST_DECREMENT;
        break;

    default:
        return token;
    }

    /* Automatic semicolon insertion. */

    if (parser->lexer->prev_token == NJS_TOKEN_LINE_END) {
        ret = njs_lexer_rollback(vm, parser->lexer);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_TOKEN_ERROR;
        }

        return NJS_TOKEN_SEMICOLON;
    }

    if (!njs_parser_is_lvalue(parser->node)) {
        njs_parser_ref_error(vm, parser,
                             "Invalid left-hand side in postfix operation");
        return NJS_TOKEN_ILLEGAL;
    }

    node = njs_parser_node_new(vm, parser, token);
    if (njs_slow_path(node == NULL)) {
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
    njs_parser_enter(vm, parser);

    if (token == NJS_TOKEN_NEW) {
        token = njs_parser_new_expression(vm, parser, token);

    } else {
        token = njs_parser_terminal(vm, parser, token);
    }

    njs_parser_leave(parser);

    if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    for ( ;; ) {
        token = njs_parser_property_expression(vm, parser, token);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token != NJS_TOKEN_OPEN_PARENTHESIS && token != NJS_TOKEN_GRAVE) {
            return token;
        }

        njs_parser_enter(vm, parser);

        token = njs_parser_call(vm, parser, token, 0);

        njs_parser_leave(parser);

        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }
    }
}


static njs_token_t
njs_parser_call(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token,
    uint8_t ctor)
{
    njs_parser_node_t  *func, *node;

    node = parser->node;

    switch (node->token) {

    case NJS_TOKEN_NAME:
        func = node;
        func->token = NJS_TOKEN_FUNCTION_CALL;

        break;

    case NJS_TOKEN_PROPERTY:
        func = njs_parser_node_new(vm, parser, NJS_TOKEN_METHOD_CALL);
        if (njs_slow_path(func == NULL)) {
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
        if (njs_slow_path(func == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        func->left = node;

        break;
    }

    func->ctor = ctor;

    switch (token) {

    case NJS_TOKEN_OPEN_PARENTHESIS:
        token = njs_parser_arguments(vm, parser, func);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        break;

    case NJS_TOKEN_GRAVE:
        token = njs_parser_template_literal(vm, parser, func);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        break;

    default:
        break;
    }

    parser->node = func;

    return token;
}


static njs_token_t
njs_parser_new_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    token = njs_parser_token(vm, parser);
    if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    njs_parser_enter(vm, parser);

    if (token == NJS_TOKEN_NEW) {
        token = njs_parser_new_expression(vm, parser, token);

    } else {
        token = njs_parser_terminal(vm, parser, token);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            njs_parser_leave(parser);
            return token;
        }

        token = njs_parser_property_expression(vm, parser, token);
    }

    njs_parser_leave(parser);

    if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    return njs_parser_call(vm, parser, token, 1);
}


static njs_token_t
njs_parser_property_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_parser_node_t  *node, *prop_node;

    for ( ;; ) {
        if (token != NJS_TOKEN_DOT
            && token != NJS_TOKEN_OPEN_BRACKET)
        {
            return token;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_PROPERTY);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.operation = NJS_VMCODE_PROPERTY_GET;
        node->left = parser->node;

        if (token == NJS_TOKEN_DOT) {
            token = njs_parser_token(vm, parser);

            if (token != NJS_TOKEN_NAME && !parser->lexer->keyword) {
                return NJS_TOKEN_ILLEGAL;
            }

            prop_node = njs_parser_node_string(vm, parser);
            if (njs_slow_path(prop_node == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            token = njs_parser_token(vm, parser);
            if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

        } else {
            token = njs_parser_token(vm, parser);
            if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            token = njs_parser_property_brackets(vm, parser, token);
            if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            prop_node = parser->node;
        }

        node->right = prop_node;
        parser->node = node;
    }
}


static njs_token_t
njs_parser_property_brackets(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    token = njs_parser_expression(vm, parser, token);
    if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (njs_slow_path(token != NJS_TOKEN_CLOSE_BRACKET)) {
        return NJS_TOKEN_ERROR;
    }

    return njs_parser_token(vm, parser);
}


static njs_token_t
njs_parser_arguments(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent)
{
    njs_token_t        token;
    njs_index_t        index;
    njs_parser_node_t  *node;

    index = NJS_SCOPE_CALLEE_ARGUMENTS;

    do {
        token = njs_parser_token(vm, parser);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_CLOSE_PARENTHESIS) {
            break;
        }

        token = njs_parser_assignment_expression(vm, parser, token);
        if (njs_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node = njs_parser_argument(vm, parser, parser->node, index);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        parent->right = node;
        parent = node;

        index += sizeof(njs_value_t);

    } while (token == NJS_TOKEN_COMMA);

    if (njs_slow_path(token != NJS_TOKEN_CLOSE_PARENTHESIS)) {
        return NJS_TOKEN_ILLEGAL;
    }

    return njs_parser_token(vm, parser);
}


njs_parser_node_t *
njs_parser_argument(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *expr, njs_index_t index)
{
    njs_parser_node_t  *node;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_ARGUMENT);
    if (njs_slow_path(node == NULL)) {
        return NULL;
    }

    node->index = index;

    node->left = expr;
    expr->dest = node;

    return node;
}
