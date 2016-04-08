
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_utf8.h>
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
#include <njs_regexp.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <string.h>


/*
 * The LL(2) parser.  The two lookahead tokens are required because
 * JavaScript inserts automatically semicolon at the end of line in
 *    a = 1
 *    b = 2
 * whilst
 *    a = 1
 *    + b
 * is treated as a single expiression.
 */

static njs_token_t njs_parser_statement_chain(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
static njs_token_t njs_parser_block(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_t njs_parser_function_declaration(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_parser_t *njs_parser_function_create(njs_vm_t *vm,
    njs_parser_t *parent);
static njs_token_t njs_parser_function_lambda(njs_vm_t *vm,
    njs_function_lambda_t *lambda, njs_token_t token);
static njs_token_t njs_parser_return_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_var_statement(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_t njs_parser_if_statement(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_t njs_parser_switch_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_while_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_do_while_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_for_statement(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_t njs_parser_for_in_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_continue_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_break_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_try_statement(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_t njs_parser_try_block(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_t njs_parser_throw_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_grouping_expression(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_builtin_object(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node);
static njs_token_t njs_parser_object(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *obj);
static njs_token_t njs_parser_array(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *obj);
static njs_token_t njs_parser_escape_string_create(njs_vm_t *vm,
    njs_value_t *value);


njs_parser_node_t *
njs_parser(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node;

    token = njs_parser_token(parser);

    while (token != NJS_TOKEN_END) {

        token = njs_parser_statement_chain(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return NULL;
        }

        if (token == NJS_TOKEN_CLOSE_BRACE) {
            parser->lexer->start--;
            break;
        }
    }

    node = parser->node;

    if (node != NULL && node->right != NULL) {
        if (node->right->token == NJS_TOKEN_FUNCTION) {
            node->token = NJS_TOKEN_CALL;
            return node;
        }

    } else {
        /* Empty string, just semicolons or variables declarations. */

        node = njs_parser_node_alloc(vm);
        if (nxt_slow_path(node == NULL)) {
            return NULL;
        }
    }

    node->token = NJS_TOKEN_END;

    return node;
}


static njs_token_t
njs_parser_statement_chain(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_parser_node_t  *node, *last;

    last = parser->node;

    token = njs_parser_statement(vm, parser, token);

    if (nxt_fast_path(token > NJS_TOKEN_ILLEGAL)) {

        if (parser->node != last) {
            /*
             * The statement is not empty block, not just semicolon,
             * and not variables declaration without initialization.
             */
            node = njs_parser_node_alloc(vm);
            if (nxt_slow_path(node == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            node->token = NJS_TOKEN_STATEMENT;
            node->left = last;
            node->right = parser->node;
            parser->node = node;
        }

    } else if (vm->exception == NULL) {
        vm->exception = &njs_exception_syntax_error;
    }

    return token;
}


static njs_token_t
njs_parser_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    switch (token) {

    case NJS_TOKEN_FUNCTION:
        return njs_parser_function_declaration(vm, parser);

    case NJS_TOKEN_RETURN:
        return njs_parser_return_statement(vm, parser);

    case NJS_TOKEN_VAR:
        return njs_parser_var_statement(vm, parser);

    case NJS_TOKEN_IF:
        return njs_parser_if_statement(vm, parser);

    case NJS_TOKEN_SWITCH:
        return njs_parser_switch_statement(vm, parser);

    case NJS_TOKEN_WHILE:
        return njs_parser_while_statement(vm, parser);

    case NJS_TOKEN_DO:
        return njs_parser_do_while_statement(vm, parser);

    case NJS_TOKEN_FOR:
        return njs_parser_for_statement(vm, parser);

    case NJS_TOKEN_CONTINUE:
        return njs_parser_continue_statement(vm, parser);

    case NJS_TOKEN_BREAK:
        return njs_parser_break_statement(vm, parser);

    case NJS_TOKEN_TRY:
        return njs_parser_try_statement(vm, parser);

    case NJS_TOKEN_THROW:
        return njs_parser_throw_statement(vm, parser);

    case NJS_TOKEN_SEMICOLON:
        parser->node = NULL;
        return njs_parser_token(parser);

    case NJS_TOKEN_OPEN_BRACE:
        return njs_parser_block(vm, parser);

    case NJS_TOKEN_CLOSE_BRACE:
        parser->node = NULL;
        nxt_thread_log_debug("BLOCK END");
        return token;

    default:
        token = njs_parser_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        /*
         * An expression must be terminated by semicolon,
         * or by a close curly brace or by the end of line.
         */
        switch (token) {

        case NJS_TOKEN_SEMICOLON:
            return njs_parser_token(parser);

        case NJS_TOKEN_CLOSE_BRACE:
        case NJS_TOKEN_END:
            return token;

        default:
            if (parser->lexer->prev_token == NJS_TOKEN_LINE_END) {
                return token;
            }

            return NJS_TOKEN_ILLEGAL;
        }
    }
}


static njs_token_t
njs_parser_block(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t  token;

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    parser->node = NULL;

    while (token != NJS_TOKEN_CLOSE_BRACE) {
        token = njs_parser_statement_chain(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }
    }

    return njs_parser_token(parser);
}


nxt_inline njs_token_t
njs_parser_match(njs_parser_t *parser, njs_token_t token,
    njs_token_t match)
{
    if (nxt_fast_path(token == match)) {
        return njs_parser_token(parser);
    }

    return NJS_TOKEN_ILLEGAL;
}


static njs_token_t
njs_parser_function_declaration(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_uint_t         level;
    njs_token_t        token;
    njs_value_t        *value;
    njs_variable_t     *var;
    njs_function_t     *function;
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_FUNCTION;

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token != NJS_TOKEN_NAME) {
        return NJS_TOKEN_ERROR;
    }

    var = njs_parser_variable(vm, parser, &level);
    if (nxt_slow_path(var == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    var->state = NJS_VARIABLE_DECLARED;
    node->index = var->index;

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    parser->node = node;

    function = njs_function_alloc(vm);
    if (nxt_slow_path(function == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    value = njs_variable_value(parser, node->index);
    value->data.u.function = function;
    value->type = NJS_FUNCTION;
    value->data.truth = 1;

    parser = njs_parser_function_create(vm, parser);
    if (nxt_slow_path(parser == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    function->u.lambda->u.parser = parser;

    return njs_parser_function_lambda(vm, function->u.lambda, token);
}


static njs_token_t
njs_parser_function_expression(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_uint_t             level;
    njs_token_t            token;
    njs_index_t            index;
    njs_value_t            *value;
    njs_variable_t         *var;
    njs_function_t         *function;
    njs_parser_node_t      *node;
    njs_function_lambda_t  *lambda;

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_FUNCTION_EXPRESSION;
    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_function_t);

    parser = njs_parser_function_create(vm, parser);
    if (nxt_slow_path(parser == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token == NJS_TOKEN_NAME) {
        var = njs_parser_variable(vm, parser, &level);
        if (nxt_slow_path(var == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        var->state = NJS_VARIABLE_DECLARED;

        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        index = var->index;
        value = njs_variable_value(parser, index);

        function = njs_function_alloc(vm);
        if (nxt_slow_path(function == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        value->data.u.function = function;
        value->type = NJS_FUNCTION;
        value->data.truth = 1;
        lambda = function->u.lambda;

    } else {
        /* Anonymous function. */
        lambda = nxt_mem_cache_zalloc(vm->mem_cache_pool,
                                      sizeof(njs_function_lambda_t));
        if (nxt_slow_path(lambda == NULL)) {
            return NJS_TOKEN_ERROR;
        }
    }

    node->u.value.data.u.lambda = lambda;
    lambda->u.parser = parser;

    return njs_parser_function_lambda(vm, lambda, token);
}


static njs_parser_t *
njs_parser_function_create(njs_vm_t *vm, njs_parser_t *parent)
{
    nxt_array_t   *values, *arguments;
    njs_parser_t  *parser;

    parser = nxt_mem_cache_zalloc(vm->mem_cache_pool, sizeof(njs_parser_t));
    if (nxt_slow_path(parser == NULL)) {
        return NULL;
    }

    parser->parent = parent;
    parser->lexer = parent->lexer;
    vm->parser = parser;

    arguments = nxt_array_create(4, sizeof(njs_variable_t),
                                 &njs_array_mem_proto, vm->mem_cache_pool);
    if (nxt_slow_path(arguments == NULL)) {
        return NULL;
    }

    parser->arguments = arguments;

    values = nxt_array_create(4, sizeof(njs_value_t), &njs_array_mem_proto,
                              vm->mem_cache_pool);
    if (nxt_slow_path(values == NULL)) {
        return NULL;
    }

    parser->scope_values = values;
    parser->scope = NJS_SCOPE_LOCAL;

    return parser;
}


static njs_token_t
njs_parser_function_lambda(njs_vm_t *vm, njs_function_lambda_t *lambda,
    njs_token_t token)
{
    nxt_str_t          *name;
    njs_index_t        index;
    njs_parser_t       *parser;
    njs_variable_t     *arg;
    njs_parser_node_t  *node, *body, *last;

    parser = lambda->u.parser;

    token = njs_parser_match(parser, token, NJS_TOKEN_OPEN_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    index = NJS_SCOPE_ARGUMENTS;

    /* A "this" reservation. */
    index += sizeof(njs_value_t);

    while (token != NJS_TOKEN_CLOSE_PARENTHESIS) {

        if (nxt_slow_path(token != NJS_TOKEN_NAME)) {
            return NJS_TOKEN_ERROR;
        }

        arg = nxt_array_add(parser->arguments, &njs_array_mem_proto,
                            vm->mem_cache_pool);
        if (nxt_slow_path(arg == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        name = &parser->lexer->text;

        arg->name_start = nxt_mem_cache_alloc(vm->mem_cache_pool, name->len);
        if (nxt_slow_path(arg->name_start == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        memcpy(arg->name_start, name->data, name->len);
        arg->name_len = name->len;

        arg->state = NJS_VARIABLE_DECLARED;
        arg->index = index;
        index += sizeof(njs_value_t);

        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_COMMA) {
            token = njs_parser_token(parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }
        }
    }

    lambda->nargs = njs_index_size(index) / sizeof(njs_value_t) - 1;

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (nxt_slow_path(token != NJS_TOKEN_OPEN_BRACE)) {
        return NJS_TOKEN_ERROR;
    }

    token = njs_parser_block(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    last = NULL;
    body = parser->node;

    if (body != NULL) {
        /* Take the last function body statement. */
        last = body->right;

        if (last == NULL) {
            /*
             * The last statement is terminated by semicolon.
             * Take the last statement itself.
             */
            last = body->left;
        }
    }

    if (last == NULL || last->token != NJS_TOKEN_RETURN) {
        /*
         * There is no function body or the last function body
         * body statement is not "return" statement.
         */
        node = njs_parser_node_alloc(vm);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->token = NJS_TOKEN_STATEMENT;
        node->left = parser->node;
        parser->node = node;

        node->right = njs_parser_node_alloc(vm);
        if (nxt_slow_path(node->right == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->right->token = NJS_TOKEN_RETURN;

        parser->code_size += sizeof(njs_vmcode_return_t);
    }

    parser->parent->node->right = parser->node;
    vm->parser = parser->parent;

    return token;
}


static njs_token_t
njs_parser_return_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_RETURN;
    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_return_t);

    token = njs_lexer_token(parser->lexer);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    switch (token) {

    case NJS_TOKEN_SEMICOLON:
    case NJS_TOKEN_LINE_END:
        return njs_parser_token(parser);

    case NJS_TOKEN_CLOSE_BRACE:
        return token;

    default:
        token = njs_parser_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (parser->node->token == NJS_TOKEN_FUNCTION) {
            /* TODO: test closure */
        }

        node->right = parser->node;
        parser->node = node;

        return token;
    }
}


static njs_token_t
njs_parser_var_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_bool_t         first;
    nxt_uint_t         level;
    njs_token_t        token;
    njs_variable_t     *var;
    njs_parser_node_t  *left, *stmt, *name, *assign;

    left = NULL;

    do {
        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token != NJS_TOKEN_NAME) {
            /* TODO: message. */
            return NJS_TOKEN_ILLEGAL;
        }

        nxt_thread_log_debug("JS: %V", &parser->lexer->text);

        var = njs_parser_variable(vm, parser, &level);
        if (nxt_slow_path(var == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        first = (var->state == NJS_VARIABLE_CREATED);

        var->state = NJS_VARIABLE_DECLARED;

        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_ASSIGNMENT) {

            token = njs_parser_token(parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            token = njs_parser_var_expression(vm, parser, token);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            name = njs_parser_node_alloc(vm);
            if (nxt_slow_path(name == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            name->token = NJS_TOKEN_NAME;
            name->lvalue = NJS_LVALUE_ENABLED;
            name->u.variable = var;

            if (first) {
                name->state = NJS_VARIABLE_FIRST_ASSIGNMENT;
            }

            assign = njs_parser_node_alloc(vm);
            if (nxt_slow_path(assign == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            assign->token = NJS_TOKEN_ASSIGNMENT;
            assign->u.operation = njs_vmcode_move;
            assign->left = name;
            assign->right = parser->node;

            stmt = njs_parser_node_alloc(vm);
            if (nxt_slow_path(stmt == NULL)) {
                return NJS_TOKEN_ERROR;
            }

            stmt->token = NJS_TOKEN_STATEMENT;
            stmt->left = left;
            stmt->right = assign;
            parser->node = stmt;
            parser->code_size += sizeof(njs_vmcode_2addr_t);

            left = stmt;
        }

    } while (token == NJS_TOKEN_COMMA);

    return token;
}


static njs_token_t
njs_parser_if_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *cond, *stmt;

    parser->branch = 1;

    token = njs_parser_grouping_expression(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    cond = parser->node;

    token = njs_parser_statement(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token == NJS_TOKEN_ELSE) {

        stmt = parser->node;

        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_statement(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node = njs_parser_node_alloc(vm);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->token = NJS_TOKEN_BRANCHING;
        node->left = stmt;
        node->right = parser->node;
        parser->node = node;
        parser->code_size += sizeof(njs_vmcode_jump_t);
    }

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_IF;
    node->left = cond;
    node->right = parser->node;
    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_cond_jump_t);

    return token;
}


static njs_token_t
njs_parser_switch_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *swtch, *branch, *dflt, **last;

    parser->branch = 1;

    node = NULL;
    branch = NULL;
    dflt = NULL;

    token = njs_parser_grouping_expression(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    swtch = njs_parser_node_alloc(vm);
    if (nxt_slow_path(swtch == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    swtch->token = NJS_TOKEN_SWITCH;
    swtch->left = parser->node;
    last = &swtch->right;

    token = njs_parser_match(parser, token, NJS_TOKEN_OPEN_BRACE);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    while (token != NJS_TOKEN_CLOSE_BRACE) {

        if (token == NJS_TOKEN_CASE || token == NJS_TOKEN_DEFAULT) {

            do {
                node = njs_parser_node_alloc(vm);
                if (nxt_slow_path(node == NULL)) {
                    return NJS_TOKEN_ERROR;
                }

                if (token == NJS_TOKEN_CASE) {
                    token = njs_parser_token(parser);
                    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                        return token;
                    }

                    token = njs_parser_expression(vm, parser, token);
                    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                        return token;
                    }

                    node->left = parser->node;

                    branch = njs_parser_node_alloc(vm);
                    if (nxt_slow_path(branch == NULL)) {
                        return NJS_TOKEN_ERROR;
                    }

                    branch->right = node;

                    parser->code_size += sizeof(njs_vmcode_equal_jump_t);

               } else {
                    if (dflt != NULL) {
                        /* A duplicate "default" branch. */
                        return NJS_TOKEN_ILLEGAL;
                    }

                    branch = node;
                    branch->token = NJS_TOKEN_DEFAULT;
                    dflt = branch;

                    token = njs_parser_token(parser);
                    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                        return token;
                    }
                }

                *last = branch;
                last = &branch->left;

                token = njs_parser_match(parser, token, NJS_TOKEN_COLON);
                if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                    return token;
                }

            } while (token == NJS_TOKEN_CASE || token == NJS_TOKEN_DEFAULT);

            parser->node = NULL;

        } else if (branch == NULL) {
            /* The first switch statment is not "case/default" keyword. */
            return NJS_TOKEN_ILLEGAL;
        }

        token = njs_parser_statement_chain(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node->right = parser->node;
    }

    parser->node = swtch;
    parser->code_size += sizeof(njs_vmcode_move_t) + sizeof(njs_vmcode_jump_t);

    return njs_parser_token(parser);
}


static njs_token_t
njs_parser_while_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *cond;

    parser->branch = 1;

    token = njs_parser_grouping_expression(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    cond = parser->node;

    token = njs_parser_statement(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_WHILE;
    node->left = parser->node;
    node->right = cond;
    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_jump_t)
                         + sizeof(njs_vmcode_cond_jump_t);

    return token;
}


static njs_token_t
njs_parser_do_while_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *stmt;

    parser->branch = 1;

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_statement(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    stmt = parser->node;

    if (nxt_slow_path(token != NJS_TOKEN_WHILE)) {
        return NJS_TOKEN_ILLEGAL;
    }

    token = njs_parser_grouping_expression(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ILLEGAL;
    }

    node->token = NJS_TOKEN_DO;
    node->left = stmt;
    node->right = parser->node;
    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_cond_jump_t);

    return token;
}


static njs_token_t
njs_parser_for_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *init, *condition, *update, *cond, *body;

    parser->branch = 1;

    init = NULL;
    condition = NULL;
    update = NULL;

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_match(parser, token, NJS_TOKEN_OPEN_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token != NJS_TOKEN_SEMICOLON) {

        token = njs_parser_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        init = parser->node;

        if (init->token == NJS_TOKEN_IN) {
            return njs_parser_for_in_statement(vm, parser, token);
        }
    }

    token = njs_parser_match(parser, token, NJS_TOKEN_SEMICOLON);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token != NJS_TOKEN_SEMICOLON) {

        token = njs_parser_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        condition = parser->node;
    }

    token = njs_parser_match(parser, token, NJS_TOKEN_SEMICOLON);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token != NJS_TOKEN_CLOSE_PARENTHESIS) {

        token = njs_parser_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        update = parser->node;
    }

    token = njs_parser_match(parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_statement(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    cond = njs_parser_node_alloc(vm);
    if (nxt_slow_path(cond == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    body = njs_parser_node_alloc(vm);
    if (nxt_slow_path(body == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_FOR;
    node->left = init;
    node->right = cond;

    cond->left = condition;
    cond->right = body;

    body->left = parser->node;
    body->right = update;

    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_jump_t)
                         + sizeof(njs_vmcode_cond_jump_t);
    return token;
}


static njs_token_t
njs_parser_for_in_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    njs_parser_node_t  *node;

    node = parser->node->left;

    if (node->token != NJS_TOKEN_NAME) {
        return NJS_TOKEN_ILLEGAL;
    }

    node->u.variable->state = NJS_VARIABLE_DECLARED;

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_FOR_IN;
    node->left = parser->node;

    token = njs_parser_match(parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_statement(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node->right = parser->node;
    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_prop_foreach_t)
                         + sizeof(njs_vmcode_prop_next_t);
    return token;
}


static njs_token_t
njs_parser_continue_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_CONTINUE;
    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_jump_t);

    token = njs_lexer_token(parser->lexer);

    switch (token) {

    case NJS_TOKEN_SEMICOLON:
    case NJS_TOKEN_LINE_END:
        return njs_parser_token(parser);

    case NJS_TOKEN_CLOSE_BRACE:
    case NJS_TOKEN_END:
        return token;

    default:
        /* TODO: LABEL */
        return NJS_TOKEN_ILLEGAL;
    }
}


static njs_token_t
njs_parser_break_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node;

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_BREAK;
    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_jump_t);

    token = njs_lexer_token(parser->lexer);

    switch (token) {

    case NJS_TOKEN_SEMICOLON:
    case NJS_TOKEN_LINE_END:
        return njs_parser_token(parser);

    case NJS_TOKEN_CLOSE_BRACE:
    case NJS_TOKEN_END:
        return token;

    default:
        /* TODO: LABEL */
        return NJS_TOKEN_ILLEGAL;
    }
}


static njs_token_t
njs_parser_try_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_uint_t         level;
    njs_token_t        token;
    njs_variable_t     *var;
    njs_parser_node_t  *node, *try, *catch;

    parser->branch = 1;

    token = njs_parser_try_block(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    try = njs_parser_node_alloc(vm);
    if (nxt_slow_path(try == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    try->token = NJS_TOKEN_TRY;
    try->left = parser->node;
    parser->code_size += sizeof(njs_vmcode_try_start_t)
                         + sizeof(njs_vmcode_try_end_t);

    if (token == NJS_TOKEN_CATCH) {
        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_match(parser, token, NJS_TOKEN_OPEN_PARENTHESIS);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token != NJS_TOKEN_NAME) {
            return NJS_TOKEN_ILLEGAL;
        }

        nxt_thread_log_debug("CATCH: %V", &parser->lexer->text);

        catch = njs_parser_node_alloc(vm);
        if (nxt_slow_path(catch == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        catch->token = NJS_TOKEN_CATCH;
        try->right = catch;

        var = njs_parser_variable(vm, parser, &level);
        if (nxt_slow_path(var == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        var->state = NJS_VARIABLE_DECLARED;

        node = njs_parser_node_alloc(vm);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->token = NJS_TOKEN_NAME;
        node->u.variable = var;

        catch->left = node;

        parser->code_size += sizeof(njs_vmcode_catch_t);

        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (nxt_slow_path(token != NJS_TOKEN_CLOSE_PARENTHESIS)) {
            return token;
        }

        token = njs_parser_try_block(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        catch->right = parser->node;

        /* TODO: remove variable from scope. */
    }

    if (token == NJS_TOKEN_FINALLY) {

        token = njs_parser_try_block(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node = njs_parser_node_alloc(vm);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->token = NJS_TOKEN_FINALLY;
        node->right = parser->node;

        if (try->right != NULL) {
            node->left = try->right;
            parser->code_size += sizeof(njs_vmcode_try_end_t);
        }

        try->right = node;
        parser->code_size += sizeof(njs_vmcode_catch_t)
                             + sizeof(njs_vmcode_finally_t);
    }

    if (try->right == NULL) {
        /* TODO: message */
        return NJS_TOKEN_ILLEGAL;
    }

    parser->node = try;

    return token;

}


static njs_token_t
njs_parser_try_block(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t  token;

    token = njs_parser_token(parser);

    if (nxt_fast_path(token == NJS_TOKEN_OPEN_BRACE)) {
        parser->node = NULL;
        return njs_parser_block(vm, parser);
    }

    return NJS_TOKEN_ILLEGAL;
}


static njs_token_t
njs_parser_throw_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node;

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = NJS_TOKEN_THROW;

    token = njs_parser_expression(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node->right = parser->node;
    parser->node = node;

    parser->code_size += sizeof(njs_vmcode_throw_t);

    return token;
}


static njs_token_t
njs_parser_grouping_expression(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t  token;

    token = njs_parser_token(parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_match(parser, token, NJS_TOKEN_OPEN_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_expression(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    return njs_parser_match(parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
}


njs_token_t
njs_parser_token(njs_parser_t *parser)
{
    njs_token_t  token;

    do {
        token = njs_lexer_token(parser->lexer);

        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

    } while (nxt_slow_path(token == NJS_TOKEN_LINE_END));

    return token;
}


njs_token_t
njs_parser_terminal(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    double             num;
    nxt_int_t          ret;
    nxt_uint_t         level;
    njs_extern_t       *ext;
    njs_variable_t     *var;
    njs_parser_node_t  *node;

    if (token == NJS_TOKEN_OPEN_PARENTHESIS) {

        token = njs_lexer_token(parser->lexer);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        return njs_parser_match(parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
    }

    if (token == NJS_TOKEN_FUNCTION) {
        return njs_parser_function_expression(vm, parser);
    }

    node = njs_parser_node_alloc(vm);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token = token;

    switch (token) {

    case NJS_TOKEN_NAME:
        nxt_thread_log_debug("JS: %V", &parser->lexer->text);

        ext = njs_parser_external(vm, parser);

        if (ext != NULL) {
            node->token = NJS_TOKEN_EXTERNAL;
            node->u.value.type = NJS_EXTERNAL;
            node->u.value.data.truth = 1;
            node->index = (njs_index_t) ext;
            break;
        }

        var = njs_parser_variable(vm, parser, &level);
        if (nxt_slow_path(var == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        switch (var->state) {

        case NJS_VARIABLE_CREATED:
            var->state = NJS_VARIABLE_PENDING;
            parser->code_size += sizeof(njs_vmcode_1addr_t);
            break;

        case NJS_VARIABLE_PENDING:
            var->state = NJS_VARIABLE_USED;
            parser->code_size += sizeof(njs_vmcode_1addr_t);
            break;

        case NJS_VARIABLE_USED:
            parser->code_size += sizeof(njs_vmcode_1addr_t);
            break;

        case NJS_VARIABLE_SET:
        case NJS_VARIABLE_DECLARED:
            break;
        }

        parser->code_size += sizeof(njs_vmcode_object_copy_t);
        node->lvalue = NJS_LVALUE_ENABLED;
        node->u.variable = var;
        break;

    case NJS_TOKEN_OPEN_BRACE:
        node->token = NJS_TOKEN_OBJECT;

        nxt_thread_log_debug("JS: OBJECT");

        parser->node = node;

        token = njs_parser_object(vm, parser, node);

        if (parser->node != node) {
            /* The object is not empty. */
            node->left = parser->node;
            parser->node = node;
        }

        parser->code_size += sizeof(njs_vmcode_1addr_t);

        return token;

    case NJS_TOKEN_OPEN_BRACKET:
        node->token = NJS_TOKEN_ARRAY;

        nxt_thread_log_debug("JS: ARRAY");

        parser->node = node;

        token = njs_parser_array(vm, parser, node);

        if (parser->node != node) {
            /* The array is not empty. */
            node->left = parser->node;
            parser->node = node;
        }

        parser->code_size += sizeof(njs_vmcode_2addr_t);

        return token;

    case NJS_TOKEN_DIVISION:
        ret = njs_regexp_literal(vm, parser, &node->u.value);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ILLEGAL;
        }

        nxt_thread_log_debug("REGEX: '%V'", &parser->lexer->text);

        node->token = NJS_TOKEN_REGEXP;
        parser->code_size += sizeof(njs_vmcode_regexp_t);

        break;

    case NJS_TOKEN_STRING:
        nxt_thread_log_debug("JS: '%V'", &parser->lexer->text);

        ret = njs_parser_string_create(vm, &node->u.value);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        break;

    case NJS_TOKEN_ESCAPE_STRING:
        node->token = NJS_TOKEN_STRING;

        nxt_thread_log_debug("JS: '%V'", &parser->lexer->text);

        ret = njs_parser_escape_string_create(vm, &node->u.value);
        if (nxt_slow_path(ret != NJS_TOKEN_STRING)) {
            return ret;
        }

        break;

    case NJS_TOKEN_NUMBER:
        nxt_thread_log_debug("JS: %f", parser->lexer->number);

        num = parser->lexer->number;
        node->u.value.data.u.number = num;
        node->u.value.type = NJS_NUMBER;
        node->u.value.data.truth = njs_is_number_true(num);

        break;

    case NJS_TOKEN_BOOLEAN:
        nxt_thread_log_debug("JS: boolean: %V", &parser->lexer->text);

        if (parser->lexer->number == 0) {
            node->u.value = njs_value_false;

        } else {
            node->u.value = njs_value_true;
        }

        break;

    case NJS_TOKEN_NULL:
        nxt_thread_log_debug("JS: null");

        node->u.value = njs_value_null;
        break;

    case NJS_TOKEN_UNDEFINED:
        nxt_thread_log_debug("JS: undefined");

        node->u.value = njs_value_void;
        break;

    case NJS_TOKEN_THIS:
        nxt_thread_log_debug("JS: this");

        node->index = NJS_INDEX_THIS;
        break;

    case NJS_TOKEN_MATH:
        return njs_parser_builtin_object(vm, parser, node);

    case NJS_TOKEN_OBJECT_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT;
        break;

    case NJS_TOKEN_ARRAY_CONSTRUCTOR:
        node->index = NJS_INDEX_ARRAY;
        break;

    case NJS_TOKEN_BOOLEAN_CONSTRUCTOR:
        node->index = NJS_INDEX_BOOLEAN;
        break;

    case NJS_TOKEN_NUMBER_CONSTRUCTOR:
        node->index = NJS_INDEX_NUMBER;
        break;

    case NJS_TOKEN_STRING_CONSTRUCTOR:
        node->index = NJS_INDEX_STRING;
        break;

    case NJS_TOKEN_FUNCTION_CONSTRUCTOR:
        node->index = NJS_INDEX_FUNCTION;
        break;

    case NJS_TOKEN_REGEXP_CONSTRUCTOR:
        node->index = NJS_INDEX_REGEXP;
        break;

    case NJS_TOKEN_DATE_CONSTRUCTOR:
        node->index = NJS_INDEX_DATE;
        break;

    case NJS_TOKEN_EVAL:
        node->index = NJS_INDEX_EVAL;
        break;

    default:
        vm->exception = &njs_exception_syntax_error;
        return NJS_TOKEN_ILLEGAL;
    }

    parser->node = node;

    return njs_lexer_token(parser->lexer);
}


static njs_token_t
njs_parser_builtin_object(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node)
{
    nxt_uint_t      index, level;
    njs_value_t     *value;
    njs_variable_t  *var;

    var = njs_parser_variable(vm, parser, &level);
    if (nxt_slow_path(var == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    var->state = NJS_VARIABLE_DECLARED;
    node->index = var->index;

    value = njs_variable_value(parser, node->index);

    index = node->token - NJS_TOKEN_FIRST_OBJECT;
    value->data.u.object = &vm->shared->objects[index];
    value->type = NJS_OBJECT;
    value->data.truth = 1;

    parser->node = node;
    parser->code_size += sizeof(njs_vmcode_object_copy_t);

    return njs_lexer_token(parser->lexer);
}


static njs_token_t
njs_parser_object(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *obj)
{
    njs_token_t        token;
    njs_parser_node_t  *stmt, *assign, *object, *propref, *left;

    left = NULL;

    for ( ;; ) {
        token = njs_parser_token(parser);

        switch (token) {

        case NJS_TOKEN_CLOSE_BRACE:
            return njs_parser_token(parser);

        case NJS_TOKEN_NAME:
            token = njs_parser_property_name(vm, parser, token);
            break;

        default:
            token = njs_parser_terminal(vm, parser, token);
            break;
        }

        object = njs_parser_node_alloc(vm);
        if (nxt_slow_path(object == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        object->token = NJS_TOKEN_OBJECT_VALUE;
        object->u.object = obj;

        propref = njs_parser_node_alloc(vm);
        if (nxt_slow_path(propref == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        propref->token = NJS_TOKEN_PROPERTY;
        propref->left = object;
        propref->right = parser->node;
        parser->code_size += sizeof(njs_vmcode_3addr_t);

        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_match(parser, token, NJS_TOKEN_COLON);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_conditional_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        assign = njs_parser_node_alloc(vm);
        if (nxt_slow_path(assign == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        assign->token = NJS_TOKEN_ASSIGNMENT;
        assign->u.operation = njs_vmcode_move;
        assign->left = propref;
        assign->right = parser->node;

        stmt = njs_parser_node_alloc(vm);
        if (nxt_slow_path(stmt == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        stmt->token = NJS_TOKEN_STATEMENT;
        stmt->left = left;
        stmt->right = assign;

        parser->code_size += sizeof(njs_vmcode_2addr_t);
        parser->node = stmt;

        left = stmt;

        if (token == NJS_TOKEN_CLOSE_BRACE) {
            return njs_parser_token(parser);
        }

        if (nxt_slow_path(token != NJS_TOKEN_COMMA)) {
            return NJS_TOKEN_ILLEGAL;
        }
    }
}


static njs_token_t
njs_parser_array(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *obj)
{
    nxt_uint_t         index;
    njs_token_t        token;
    njs_parser_node_t  *stmt, *assign, *object, *propref, *left, *node;

    index = 0;
    left = NULL;

    for ( ;; ) {
        token = njs_parser_token(parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (token == NJS_TOKEN_COMMA) {
            index++;
            continue;
        }

        node = njs_parser_node_alloc(vm);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->token = NJS_TOKEN_NUMBER;
        node->u.value.data.u.number = index;
        node->u.value.type = NJS_NUMBER;
        node->u.value.data.truth = (index != 0);
        index++;

        object = njs_parser_node_alloc(vm);
        if (nxt_slow_path(object == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        object->token = NJS_TOKEN_OBJECT_VALUE;
        object->u.object = obj;

        propref = njs_parser_node_alloc(vm);
        if (nxt_slow_path(propref == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        propref->token = NJS_TOKEN_PROPERTY;
        propref->left = object;
        propref->right = node;
        parser->code_size += sizeof(njs_vmcode_3addr_t);

        token = njs_parser_conditional_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        assign = njs_parser_node_alloc(vm);
        if (nxt_slow_path(assign == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        assign->token = NJS_TOKEN_ASSIGNMENT;
        assign->u.operation = njs_vmcode_move;
        assign->left = propref;
        assign->right = parser->node;

        stmt = njs_parser_node_alloc(vm);
        if (nxt_slow_path(stmt == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        stmt->token = NJS_TOKEN_STATEMENT;
        stmt->left = left;
        stmt->right = assign;

        parser->code_size += sizeof(njs_vmcode_2addr_t);
        parser->node = stmt;

        left = stmt;

        if (token == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (nxt_slow_path(token != NJS_TOKEN_COMMA)) {
            return NJS_TOKEN_ILLEGAL;
        }
    }

    obj->u.length = index;

    return njs_parser_token(parser);
}


nxt_int_t
njs_parser_string_create(njs_vm_t *vm, njs_value_t *value)
{
    u_char     *p;
    ssize_t    length;
    nxt_str_t  *src;

    src = &vm->parser->lexer->text;

    length = nxt_utf8_length(src->data, src->len);

    if (nxt_slow_path(length < 0)) {
        length = 0;
    }

    p = njs_string_alloc(vm, value, src->len, length);

    if (nxt_fast_path(p != NULL)) {
        memcpy(p, src->data, src->len);

        if (length > NJS_STRING_MAP_OFFSET && (size_t) length != src->len) {
            njs_string_offset_map_init(p, src->len);
        }

        return NXT_OK;
    }

    return NXT_ERROR;
}


static njs_token_t
njs_parser_escape_string_create(njs_vm_t *vm, njs_value_t *value)
{
    u_char   c, *p, *start, *dst, *src, *end, *hex_end;
    size_t   size, length, hex_length, skip;
    int64_t  u;

    start = NULL;
    dst = NULL;

    for ( ;; ) {
        /*
         * The loop runs twice: at the first step string size and
         * UTF-8 length are evaluated.  Then the string is allocated
         * and at the second step string content is copied.
         */
        size = 0;
        length = 0;

        src = vm->parser->lexer->text.data;
        end = src + vm->parser->lexer->text.len;

        while (src < end) {
            c = *src++;

            if (c == '\\') {
                /*
                 * Testing "src == end" is not required here
                 * since this has been already tested by lexer.
                 */
                c = *src++;

                switch (c) {

                case 'u':
                    skip = 0;
                    hex_length = 4;

                    /*
                     * A character after "u" can be safely tested here
                     * because there is always a closing quote at the
                     * end of string: ...\u".
                     */
                    if (*src == '{') {
                        hex_length = 0;
                        src++;

                        for (p = src; p < end && *p != '}'; p++) {
                            hex_length++;
                        }

                        if (hex_length == 0 || hex_length > 6) {
                            return NJS_TOKEN_ILLEGAL;
                        }

                        skip = 1;
                    }

                    goto hex;

                case 'x':
                    skip = 0;
                    hex_length = 2;
                    goto hex;

                case '0':
                    c = '\0';
                    break;

                case 'b':
                    c = '\b';
                    break;

                case 'f':
                    c = '\f';
                    break;

                case 'n':
                    c = '\n';
                    break;

                case 'r':
                    c = '\r';
                    break;

                case 't':
                    c = '\t';
                    break;

                case 'v':
                    c = '\v';
                    break;

                case '\r':
                    /*
                     * A character after "\r" can be safely tested here
                     * because there is always a closing quote at the
                     * end of string: ...\\r".
                     */
                    if (*src == '\n') {
                        src++;
                    }

                    continue;

                case '\n':
                    continue;

                default:
                    break;
                }
            }

            size++;
            length++;

            if (dst != NULL) {
                *dst++ = c;
            }

            continue;

        hex:

            hex_end = src + hex_length;

            if (hex_end > end) {
                return NJS_TOKEN_ILLEGAL;
            }

            u = njs_hex_number_parse(src, hex_end);
            if (nxt_slow_path(u < 0)) {
                return NJS_TOKEN_ILLEGAL;
            }

            src = hex_end + skip;
            size += nxt_utf8_size(u);
            length++;

            if (dst != NULL) {
                dst = nxt_utf8_encode(dst, (uint32_t) u);
            }
        }

        if (start != NULL) {
            if (length > NJS_STRING_MAP_OFFSET && length != size) {
                njs_string_offset_map_init(start, size);
            }

            return NJS_TOKEN_STRING;
        }

        start = njs_string_alloc(vm, value, size, length);
        if (nxt_slow_path(start == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        dst = start;
    }
}


njs_index_t
njs_parser_index(njs_parser_t *parser, uint32_t scope)
{
    nxt_uint_t   n;
    njs_index_t  index;

    /* Skip absolute scope. */
    n = scope - NJS_INDEX_CACHE;

    index = parser->index[n];
    parser->index[n] += sizeof(njs_value_t);

    index |= scope;

    nxt_thread_log_debug("GET %p", index);

    return index;
}


nxt_bool_t
njs_parser_has_side_effect(njs_parser_node_t *node)
{
    nxt_bool_t  side_effect;

    if (node == NULL) {
        return 0;
    }

    if (node->token >= NJS_TOKEN_ASSIGNMENT
        && node->token <= NJS_TOKEN_LAST_ASSIGNMENT)
    {
        return 1;
    }

    if (node->token == NJS_TOKEN_FUNCTION_CALL
        || node->token == NJS_TOKEN_METHOD_CALL)
    {
        return 1;
    }

    side_effect = njs_parser_has_side_effect(node->left);

    if (nxt_fast_path(!side_effect)) {
        return njs_parser_has_side_effect(node->right);
    }

    return side_effect;
}
