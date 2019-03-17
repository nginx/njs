
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <njs_regexp.h>
#include <string.h>
#include <stdio.h>


static njs_ret_t njs_parser_scope_begin(njs_vm_t *vm, njs_parser_t *parser,
    njs_scope_t type);
static void njs_parser_scope_end(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_t njs_parser_statement_chain(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token, njs_parser_node_t **dest);
static njs_token_t njs_parser_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
static njs_token_t njs_parser_block_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_block(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_labelled_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_function_declaration(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_function_lambda(njs_vm_t *vm,
    njs_parser_t *parser, njs_function_lambda_t *lambda, njs_token_t token);
static njs_token_t njs_parser_lambda_arguments(njs_vm_t *vm,
    njs_parser_t *parser, njs_function_lambda_t *lambda, njs_index_t index,
    njs_token_t token);
static njs_token_t njs_parser_lambda_argument(njs_vm_t *vm,
    njs_parser_t *parser, njs_index_t index);
static njs_token_t njs_parser_lambda_body(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
static njs_parser_node_t *njs_parser_return_set(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *expr);
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
static njs_token_t njs_parser_for_var_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_for_var_in_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *name);
static njs_token_t njs_parser_for_in_statement(njs_vm_t *vm,
    njs_parser_t *parser, nxt_str_t *name, njs_token_t token);
static njs_token_t njs_parser_brk_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
static njs_token_t njs_parser_try_statement(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_t njs_parser_try_block(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_t njs_parser_throw_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_t njs_parser_grouping_expression(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_parser_node_t *njs_parser_reference(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token, nxt_str_t *name, uint32_t hash,
    uint32_t token_line);
static nxt_int_t njs_parser_builtin(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node, njs_value_type_t type, nxt_str_t *name,
    uint32_t hash);
static njs_token_t njs_parser_object(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *obj);
static njs_token_t njs_parser_array(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *obj);
static nxt_int_t njs_parser_string_create(njs_vm_t *vm, njs_value_t *value);
static njs_token_t njs_parser_escape_string_create(njs_vm_t *vm,
    njs_parser_t *parser, njs_value_t *value);
static njs_token_t njs_parser_unexpected_token(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);


#define njs_parser_chain_current(parser)                            \
    ((parser)->node)

#define njs_parser_chain_top(parser)                                \
    ((parser)->scope->top)

#define njs_parser_chain_top_set(parser, node)                      \
    (parser)->scope->top = node

#define njs_parser_text(parser)                                     \
    &(parser)->lexer->lexer_token->text

#define njs_parser_key_hash(parser)                                 \
    (parser)->lexer->lexer_token->key_hash

#define njs_parser_number(parser)                                 \
    (parser)->lexer->lexer_token->number

#define njs_parser_token_line(parser)                                 \
    (parser)->lexer->lexer_token->token_line


nxt_int_t
njs_parser(njs_vm_t *vm, njs_parser_t *parser, njs_parser_t *prev)
{
    njs_ret_t           ret;
    njs_token_t         token;
    nxt_lvlhsh_t        *variables, *prev_variables;
    njs_variable_t      *var;
    njs_parser_node_t   *node;
    nxt_lvlhsh_each_t   lhe;
    nxt_lvlhsh_query_t  lhq;

    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_GLOBAL);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    if (prev != NULL) {
        /*
         * Copy the global scope variables from the previous
         * iteration of the accumulative mode.
         */
        nxt_lvlhsh_each_init(&lhe, &njs_variables_hash_proto);

        lhq.proto = &njs_variables_hash_proto;
        lhq.replace = 0;
        lhq.pool = vm->mem_pool;

        variables = &parser->scope->variables;
        prev_variables = &prev->scope->variables;

        for ( ;; ) {
            var = nxt_lvlhsh_each(prev_variables, &lhe);

            if (var == NULL) {
                break;
            }

            lhq.value = var;
            lhq.key = var->name;
            lhq.key_hash = nxt_djb_hash(var->name.start, var->name.length);

            ret = nxt_lvlhsh_insert(variables, &lhq);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NXT_ERROR;
            }
        }
    }

    token = njs_parser_token(vm, parser);

    while (token != NJS_TOKEN_END) {

        token = njs_parser_statement_chain(vm, parser, token,
                                           &njs_parser_chain_top(parser));
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return NXT_ERROR;
        }

        if (token == NJS_TOKEN_CLOSE_BRACE && vm->options.trailer) {
            parser->lexer->start--;
            break;
        }
    }

    node = njs_parser_chain_top(parser);

    if (node == NULL) {
        /* Empty string, just semicolons or variables declarations. */

        node = njs_parser_node_new(vm, parser, 0);
        if (nxt_slow_path(node == NULL)) {
            return NXT_ERROR;
        }

        njs_parser_chain_top_set(parser, node);
    }

    node->token = NJS_TOKEN_END;

    return NXT_OK;
}


static njs_ret_t
njs_parser_scope_begin(njs_vm_t *vm, njs_parser_t *parser, njs_scope_t type)
{
    nxt_uint_t          nesting;
    nxt_array_t         *values;
    njs_lexer_t         *lexer;
    njs_parser_scope_t  *scope, *parent;

    nesting = 0;

    if (type == NJS_SCOPE_FUNCTION) {

        for (scope = parser->scope; scope != NULL; scope = scope->parent) {

            if (scope->type == NJS_SCOPE_FUNCTION) {
                nesting = scope->nesting + 1;

                if (nesting <= NJS_MAX_NESTING) {
                    break;
                }

                njs_parser_syntax_error(vm, parser,
                                        "The maximum function nesting "
                                        "level is \"%d\"", NJS_MAX_NESTING);

                return NXT_ERROR;
            }
        }
    }

    scope = nxt_mp_zalloc(vm->mem_pool, sizeof(njs_parser_scope_t));
    if (nxt_slow_path(scope == NULL)) {
        return NXT_ERROR;
    }

    scope->type = type;

    if (type == NJS_SCOPE_FUNCTION) {
        scope->next_index[0] = type;
        scope->next_index[1] = NJS_SCOPE_CLOSURE + nesting
                               + sizeof(njs_value_t);

    } else {
        if (type == NJS_SCOPE_GLOBAL) {
            type += NJS_INDEX_GLOBAL_OFFSET;
        }

        scope->next_index[0] = type;
        scope->next_index[1] = 0;
    }

    scope->nesting = nesting;
    scope->argument_closures = 0;

    nxt_queue_init(&scope->nested);
    nxt_lvlhsh_init(&scope->labels);
    nxt_lvlhsh_init(&scope->variables);
    nxt_lvlhsh_init(&scope->references);

    values = NULL;

    if (scope->type < NJS_SCOPE_BLOCK) {
        values = nxt_array_create(4, sizeof(njs_value_t), &njs_array_mem_proto,
                                  vm->mem_pool);
        if (nxt_slow_path(values == NULL)) {
            return NXT_ERROR;
        }
    }

    scope->values[0] = values;
    scope->values[1] = NULL;

    lexer = parser->lexer;

    if (lexer->file.length != 0) {
        nxt_file_basename(&lexer->file, &scope->file);
    }

    parent = parser->scope;
    scope->parent = parent;
    parser->scope = scope;

    if (parent != NULL) {
        nxt_queue_insert_tail(&parent->nested, &scope->link);

        if (nesting == 0) {
            /* Inherit function nesting in blocks. */
            scope->nesting = parent->nesting;
        }
    }

    return NXT_OK;
}


static void
njs_parser_scope_end(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_parser_scope_t  *scope, *parent;

    scope = parser->scope;

    parent = scope->parent;
    parser->scope = parent;
}


static njs_token_t
njs_parser_statement_chain(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token, njs_parser_node_t **dest)
{
    njs_parser_node_t  *node, *last;

    last = *dest;

    token = njs_parser_statement(vm, parser, token);

    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return njs_parser_unexpected_token(vm, parser, token);
    }

    if (parser->node != NULL) {
        /* The statement is not empty block or just semicolon. */

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->left = last;
        node->right = parser->node;
        *dest = node;

        while (token == NJS_TOKEN_SEMICOLON) {
            token = njs_parser_token(vm, parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                break;
            }
        }
    }

    return token;
}


static njs_token_t
njs_parser_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    size_t  offset;

    parser->node = NULL;

    switch (token) {

    case NJS_TOKEN_FUNCTION:
        return njs_parser_function_declaration(vm, parser);

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

    case NJS_TOKEN_TRY:
        return njs_parser_try_statement(vm, parser);

    case NJS_TOKEN_SEMICOLON:
        return njs_parser_token(vm, parser);

    case NJS_TOKEN_OPEN_BRACE:
        return njs_parser_block_statement(vm, parser);

    case NJS_TOKEN_CLOSE_BRACE:
        if (vm->options.trailer) {
            parser->node = NULL;
            nxt_thread_log_debug("BLOCK END");
            return token;
        }

        /* Fall through. */

    default:

        switch (token) {
        case NJS_TOKEN_VAR:
            token = njs_parser_var_statement(vm, parser);
            break;

        case NJS_TOKEN_RETURN:
            token = njs_parser_return_statement(vm, parser);
            break;

        case NJS_TOKEN_THROW:
            token = njs_parser_throw_statement(vm, parser);
            break;

        case NJS_TOKEN_CONTINUE:
        case NJS_TOKEN_BREAK:
            token = njs_parser_brk_statement(vm, parser, token);
            break;

        case NJS_TOKEN_NAME:
            offset = 0;
            if (njs_parser_peek_token(vm, parser, &offset) == NJS_TOKEN_COLON) {
                return njs_parser_labelled_statement(vm, parser);
            }

            /* Fall through. */

        default:
            token = njs_parser_expression(vm, parser, token);
            break;
        }

        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        /*
         * An expression must be terminated by semicolon,
         * or by a close curly brace or by the end of line.
         */
        switch (token) {

        case NJS_TOKEN_SEMICOLON:
            return njs_parser_token(vm, parser);

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
njs_parser_block_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_ret_t          ret;
    njs_token_t        token;
    njs_parser_node_t  *node;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_BLOCK);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NJS_TOKEN_ERROR;
    }

    parser->node = NULL;

    while (token != NJS_TOKEN_CLOSE_BRACE) {
        token = njs_parser_statement_chain(vm, parser, token,
                                           &njs_parser_chain_current(parser));
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }
    }

    if (parser->node != NULL) {
        /* The statement is not empty block or just semicolon. */

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_BLOCK);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->left = parser->node;
        node->right = NULL;
        parser->node = node;
    }

    njs_parser_scope_end(vm, parser);

    return njs_parser_token(vm, parser);
}


static njs_token_t
njs_parser_block(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    njs_parser_node_t  *node;

    token = njs_parser_statement(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = parser->node;

    if (node != NULL && node->token == NJS_TOKEN_BLOCK) {
        parser->node = node->left;

        nxt_mp_free(vm->mem_pool, node);
    }

    return token;
}


nxt_inline njs_token_t
njs_parser_match(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token,
    njs_token_t match)
{
    if (nxt_fast_path(token == match)) {
        return njs_parser_token(vm, parser);
    }

    return njs_parser_unexpected_token(vm, parser, token);
}


nxt_inline njs_variable_t *
njs_parser_variable_add(njs_vm_t *vm, njs_parser_t *parser,
    njs_variable_type_t type)
{
    return njs_variable_add(vm, parser->scope, njs_parser_text(parser),
                            njs_parser_key_hash(parser), type);
}

nxt_inline njs_ret_t
njs_parser_variable_reference(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node, njs_reference_type_t type)
{
    return njs_variable_reference(vm, parser->scope, node,
                                  njs_parser_text(parser),
                                  njs_parser_key_hash(parser), type);
}


static njs_token_t
njs_parser_labelled_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    uint32_t        hash;
    njs_ret_t       ret;
    nxt_str_t       name;
    njs_token_t     token;
    njs_variable_t  *label;

    name = *njs_parser_text(parser);
    hash = njs_parser_key_hash(parser);

    label = njs_label_find(vm, parser->scope, &name, hash);
    if (nxt_slow_path(label != NULL)) {
        njs_parser_syntax_error(vm, parser, "Label \"%V\" "
                                "has already been declared", &name);
        return NJS_TOKEN_ILLEGAL;
    }

    label = njs_label_add(vm, parser->scope, &name, hash);
    if (nxt_slow_path(label == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_match(vm, parser, token, NJS_TOKEN_COLON);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_statement(vm, parser, token);

    if (nxt_fast_path(token > NJS_TOKEN_ILLEGAL)) {

        if (parser->node != NULL) {
            /* The statement is not empty block or just semicolon. */

            ret = njs_name_copy(vm, &parser->node->label, &name);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NJS_TOKEN_ERROR;
            }

            ret = njs_label_remove(vm, parser->scope, &name, hash);
            if (nxt_slow_path(ret != NXT_OK)) {
                return NJS_TOKEN_ERROR;
            }
        }
    }

    return token;
}


static njs_function_t *
njs_parser_function_alloc(njs_vm_t *vm, njs_parser_t *parser,
    njs_variable_t *var)
{
    njs_value_t     *value;
    njs_function_t  *function;

    function = njs_function_alloc(vm);
    if (nxt_slow_path(function == NULL)) {
        return NULL;
    }

    var->value.data.u.function = function;
    var->value.type = NJS_FUNCTION;
    var->value.data.truth = 1;

    if (var->index != NJS_INDEX_NONE
        && njs_scope_accumulative(vm, parser->scope))
    {
        value = (njs_value_t *) var->index;
        *value = var->value;
    }

    return function;
}


static njs_token_t
njs_parser_function_declaration(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_ret_t          ret;
    njs_token_t        token;
    njs_variable_t     *var;
    njs_function_t     *function;
    njs_parser_node_t  *node;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FUNCTION);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token_line = njs_parser_token_line(parser);

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token != NJS_TOKEN_NAME) {
        if (token == NJS_TOKEN_ARGUMENTS || token == NJS_TOKEN_EVAL) {
            njs_parser_syntax_error(vm, parser, "Identifier \"%V\" "
                                    "is forbidden in function declaration",
                                    njs_parser_text(parser));
        }

        return NJS_TOKEN_ILLEGAL;
    }

    var = njs_parser_variable_add(vm, parser, NJS_VARIABLE_FUNCTION);
    if (nxt_slow_path(var == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    ret = njs_parser_variable_reference(vm, parser, node, NJS_DECLARATION);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NJS_TOKEN_ERROR;
    }

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    parser->node = node;

    function = njs_parser_function_alloc(vm, parser, var);
    if (nxt_slow_path(function == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    token = njs_parser_function_lambda(vm, parser, function->u.lambda, token);

    return token;
}


static njs_token_t
njs_parser_function_expression(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_ret_t              ret;
    njs_token_t            token;
    njs_variable_t         *var;
    njs_function_t         *function;
    njs_parser_node_t      *node;
    njs_function_lambda_t  *lambda;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FUNCTION_EXPRESSION);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token_line = njs_parser_token_line(parser);
    parser->node = node;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    /*
     * An optional function expression name is stored
     * in intermediate shim scope.
     */
    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_SHIM);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NJS_TOKEN_ERROR;
    }

    if (token == NJS_TOKEN_NAME) {
        var = njs_parser_variable_add(vm, parser, NJS_VARIABLE_SHIM);
        if (nxt_slow_path(var == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        function = njs_parser_function_alloc(vm, parser, var);
        if (nxt_slow_path(function == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        lambda = function->u.lambda;

    } else {
        /* Anonymous function. */
        lambda = nxt_mp_zalloc(vm->mem_pool, sizeof(njs_function_lambda_t));
        if (nxt_slow_path(lambda == NULL)) {
            return NJS_TOKEN_ERROR;
        }
    }

    node->u.value.data.u.lambda = lambda;

    token = njs_parser_function_lambda(vm, parser, lambda, token);

    njs_parser_scope_end(vm, parser);

    return token;
}


static njs_token_t
njs_parser_function_lambda(njs_vm_t *vm, njs_parser_t *parser,
    njs_function_lambda_t *lambda, njs_token_t token)
{
    njs_ret_t    ret;
    njs_index_t  index;

    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_FUNCTION);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NJS_TOKEN_ERROR;
    }

    index = NJS_SCOPE_ARGUMENTS;

    /* A "this" reservation. */
    index += sizeof(njs_value_t);

    token = njs_parser_lambda_arguments(vm, parser, lambda, index, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_lambda_body(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    njs_parser_scope_end(vm, parser);

    return token;
}


static njs_token_t
njs_parser_lambda_arguments(njs_vm_t *vm, njs_parser_t *parser,
    njs_function_lambda_t *lambda, njs_index_t index, njs_token_t token)
{
    token = njs_parser_match(vm, parser, token, NJS_TOKEN_OPEN_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    lambda->nargs = 0;

    while (token != NJS_TOKEN_CLOSE_PARENTHESIS) {

        if (nxt_slow_path(lambda->rest_parameters)) {
            return NJS_TOKEN_ILLEGAL;
        }

        if (nxt_slow_path(token == NJS_TOKEN_ELLIPSIS)) {
            lambda->rest_parameters = 1;

            token = njs_parser_token(vm, parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return NJS_TOKEN_ILLEGAL;
            }
        }

        if (nxt_slow_path(token != NJS_TOKEN_NAME)) {
            return NJS_TOKEN_ILLEGAL;
        }

        token = njs_parser_lambda_argument(vm, parser, index);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_COMMA) {
            token = njs_parser_token(vm, parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }
        }

        lambda->nargs++;
        index += sizeof(njs_value_t);
    }

    return njs_parser_token(vm, parser);
}


static njs_token_t
njs_parser_lambda_argument(njs_vm_t *vm, njs_parser_t *parser,
    njs_index_t index)
{
    njs_ret_t       ret;
    njs_variable_t  *arg;

    arg = njs_parser_variable_add(vm, parser, NJS_VARIABLE_VAR);
    if (nxt_slow_path(arg == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    arg->index = index;

    ret = njs_name_copy(vm, &arg->name, njs_parser_text(parser));
    if (nxt_slow_path(ret != NXT_OK)) {
        return NJS_TOKEN_ERROR;
    }

    return njs_parser_token(vm, parser);
}


static njs_token_t
njs_parser_lambda_body(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    njs_parser_node_t  *body, *last, *parent;

    parent = parser->node;

    token = njs_parser_lambda_statements(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    last = NULL;
    body = njs_parser_chain_top(parser);

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
        body = njs_parser_return_set(vm, parser, NULL);
        if (nxt_slow_path(body == NULL)) {
            return NJS_TOKEN_ERROR;
        }
    }

    parent->right = body;

    parser->node = parent;

    return token;
}


static njs_parser_node_t *
njs_parser_return_set(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *expr)
{
    njs_parser_node_t  *stmt, *node;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_RETURN);
    if (nxt_slow_path(node == NULL)) {
        return NULL;
    }

    node->right = expr;

    stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
    if (nxt_slow_path(stmt == NULL)) {
        return NULL;
    }

    stmt->left = njs_parser_chain_top(parser);
    stmt->right = node;

    njs_parser_chain_top_set(parser, stmt);

    return stmt;
}


njs_token_t
njs_parser_lambda_statements(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    token = njs_parser_match(vm, parser, token, NJS_TOKEN_OPEN_BRACE);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    parser->node = NULL;

    while (token != NJS_TOKEN_CLOSE_BRACE) {
        token = njs_parser_statement_chain(vm, parser, token,
                                           &njs_parser_chain_top(parser));
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }
    }

    return njs_parser_token(vm, parser);
}


static njs_token_t
njs_parser_return_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t         token;
    njs_parser_node_t   *node;
    njs_parser_scope_t  *scope;

    for (scope = parser->scope;
         scope->type != NJS_SCOPE_FUNCTION;
         scope = scope->parent)
    {
        if (scope->type == NJS_SCOPE_GLOBAL) {
            njs_parser_syntax_error(vm, parser, "Illegal return statement");

            return NXT_ERROR;
        }
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_RETURN);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    parser->node = node;

    token = njs_lexer_token(vm, parser->lexer);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    switch (token) {

    case NJS_TOKEN_LINE_END:
        return njs_parser_token(vm, parser);

    case NJS_TOKEN_SEMICOLON:
    case NJS_TOKEN_CLOSE_BRACE:
    case NJS_TOKEN_END:
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
    njs_ret_t          ret;
    njs_token_t        token;
    njs_variable_t     *var;
    njs_parser_node_t  *left, *stmt, *name, *assign, *expr;

    parser->node = NULL;
    left = NULL;

    do {
        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token != NJS_TOKEN_NAME) {
            if (token == NJS_TOKEN_ARGUMENTS || token == NJS_TOKEN_EVAL) {
                njs_parser_syntax_error(vm, parser, "Identifier \"%V\" "
                                        "is forbidden in var declaration",
                                        njs_parser_text(parser));
            }

            return NJS_TOKEN_ILLEGAL;
        }

        var = njs_parser_variable_add(vm, parser, NJS_VARIABLE_VAR);
        if (nxt_slow_path(var == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        name = njs_parser_node_new(vm, parser, NJS_TOKEN_NAME);
        if (nxt_slow_path(name == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_variable_reference(vm, parser, name, NJS_DECLARATION);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        expr = NULL;

        if (token == NJS_TOKEN_ASSIGNMENT) {

            token = njs_parser_token(vm, parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            token = njs_parser_var_expression(vm, parser, token);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            expr = parser->node;
        }

        assign = njs_parser_node_new(vm, parser, NJS_TOKEN_VAR);
        if (nxt_slow_path(assign == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        assign->u.operation = njs_vmcode_move;
        assign->left = name;
        assign->right = expr;

        stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
        if (nxt_slow_path(stmt == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        stmt->left = left;
        stmt->right = assign;
        parser->node = stmt;

        left = stmt;

    } while (token == NJS_TOKEN_COMMA);

    return token;
}


static njs_token_t
njs_parser_if_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *cond, *stmt;

    token = njs_parser_grouping_expression(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    cond = parser->node;

    token = njs_parser_block(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token == NJS_TOKEN_ELSE) {

        stmt = parser->node;

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_block(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_BRANCHING);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->left = stmt;
        node->right = parser->node;
        parser->node = node;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_IF);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = cond;
    node->right = parser->node;
    parser->node = node;

    return token;
}


static njs_token_t
njs_parser_switch_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *swtch, *branch, *dflt, **last;

    node = NULL;
    branch = NULL;
    dflt = NULL;

    token = njs_parser_grouping_expression(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    swtch = njs_parser_node_new(vm, parser, NJS_TOKEN_SWITCH);
    if (nxt_slow_path(swtch == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    swtch->left = parser->node;
    last = &swtch->right;

    token = njs_parser_match(vm, parser, token, NJS_TOKEN_OPEN_BRACE);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    while (token != NJS_TOKEN_CLOSE_BRACE) {

        if (token == NJS_TOKEN_CASE || token == NJS_TOKEN_DEFAULT) {

            do {
                node = njs_parser_node_new(vm, parser, 0);
                if (nxt_slow_path(node == NULL)) {
                    return NJS_TOKEN_ERROR;
                }

                if (token == NJS_TOKEN_CASE) {
                    token = njs_parser_token(vm, parser);
                    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                        return token;
                    }

                    token = njs_parser_expression(vm, parser, token);
                    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                        return token;
                    }

                    node->left = parser->node;

                    branch = njs_parser_node_new(vm, parser, 0);
                    if (nxt_slow_path(branch == NULL)) {
                        return NJS_TOKEN_ERROR;
                    }

                    branch->right = node;

               } else {
                    if (dflt != NULL) {
                        njs_parser_syntax_error(vm, parser,
                                                "More than one default clause "
                                                "in switch statement");

                        return NJS_TOKEN_ILLEGAL;
                    }

                    branch = node;
                    branch->token = NJS_TOKEN_DEFAULT;
                    dflt = branch;

                    token = njs_parser_token(vm, parser);
                    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                        return token;
                    }
                }

                *last = branch;
                last = &branch->left;

                token = njs_parser_match(vm, parser, token, NJS_TOKEN_COLON);
                if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                    return token;
                }

            } while (token == NJS_TOKEN_CASE || token == NJS_TOKEN_DEFAULT);

            parser->node = NULL;

            if (token == NJS_TOKEN_CLOSE_BRACE) {
                break;
            }

        } else if (branch == NULL) {
            /* The first switch statment is not "case/default" keyword. */
            return NJS_TOKEN_ILLEGAL;
        }

        token = njs_parser_statement_chain(vm, parser, token,
                                           &njs_parser_chain_current(parser));
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node->right = parser->node;
    }

    parser->node = swtch;

    return njs_parser_token(vm, parser);
}


static njs_token_t
njs_parser_while_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *cond;

    token = njs_parser_grouping_expression(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    cond = parser->node;

    token = njs_parser_block(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_WHILE);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = parser->node;
    node->right = cond;
    parser->node = node;

    return token;
}


static njs_token_t
njs_parser_do_while_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *stmt;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_block(vm, parser, token);
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

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_DO);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = stmt;
    node->right = parser->node;
    parser->node = node;

    return token;
}


static njs_token_t
njs_parser_for_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_str_t          name;
    njs_token_t        token;
    njs_parser_node_t  *node, *init, *condition, *update, *cond, *body;

    init = NULL;
    condition = NULL;
    update = NULL;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_match(vm, parser, token, NJS_TOKEN_OPEN_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    if (token != NJS_TOKEN_SEMICOLON) {

        if (token == NJS_TOKEN_VAR) {

            token = njs_parser_for_var_statement(vm, parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            init = parser->node;

            if (init->token == NJS_TOKEN_FOR_IN) {
                return token;
            }

        } else {

            name = *njs_parser_text(parser);

            token = njs_parser_expression(vm, parser, token);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            init = parser->node;

            if (init->token == NJS_TOKEN_IN) {
                return njs_parser_for_in_statement(vm, parser, &name, token);
            }
        }
    }

    token = njs_parser_match(vm, parser, token, NJS_TOKEN_SEMICOLON);
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

    token = njs_parser_match(vm, parser, token, NJS_TOKEN_SEMICOLON);
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

    token = njs_parser_match(vm, parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_block(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FOR);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    cond = njs_parser_node_new(vm, parser, 0);
    if (nxt_slow_path(cond == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    body = njs_parser_node_new(vm, parser, 0);
    if (nxt_slow_path(body == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = init;
    node->right = cond;

    cond->left = condition;
    cond->right = body;

    body->left = parser->node;
    body->right = update;

    parser->node = node;

    return token;
}


static njs_token_t
njs_parser_for_var_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_ret_t          ret;
    njs_token_t        token;
    njs_variable_t     *var;
    njs_parser_node_t  *left, *stmt, *name, *assign, *expr;

    parser->node = NULL;
    left = NULL;

    do {
        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token != NJS_TOKEN_NAME) {
            if (token == NJS_TOKEN_ARGUMENTS || token == NJS_TOKEN_EVAL) {
                njs_parser_syntax_error(vm, parser, "Identifier \"%V\" "
                                       "is forbidden in for-in var declaration",
                                       njs_parser_text(parser));
            }

            return NJS_TOKEN_ILLEGAL;
        }

        var = njs_parser_variable_add(vm, parser, NJS_VARIABLE_VAR);
        if (nxt_slow_path(var == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        name = njs_parser_node_new(vm, parser, NJS_TOKEN_NAME);
        if (nxt_slow_path(name == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_variable_reference(vm, parser, name, NJS_DECLARATION);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_IN) {
            return njs_parser_for_var_in_statement(vm, parser, name);
        }

        expr = NULL;

        if (token == NJS_TOKEN_ASSIGNMENT) {

            token = njs_parser_token(vm, parser);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            token = njs_parser_var_expression(vm, parser, token);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            expr = parser->node;
        }

        assign = njs_parser_node_new(vm, parser, NJS_TOKEN_VAR);
        if (nxt_slow_path(assign == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        assign->u.operation = njs_vmcode_move;
        assign->left = name;
        assign->right = expr;

        stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
        if (nxt_slow_path(stmt == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        stmt->left = left;
        stmt->right = assign;
        parser->node = stmt;

        left = stmt;

    } while (token == NJS_TOKEN_COMMA);

    return token;
}


static njs_token_t
njs_parser_for_var_in_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *name)
{
    njs_token_t        token;
    njs_parser_node_t  *node, *foreach;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_expression(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_IN);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = name;
    node->right = parser->node;

    token = njs_parser_match(vm, parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_block(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    foreach = njs_parser_node_new(vm, parser, NJS_TOKEN_FOR_IN);
    if (nxt_slow_path(foreach == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    foreach->left = node;
    foreach->right = parser->node;

    parser->node = foreach;

    return token;
}


static njs_token_t
njs_parser_for_in_statement(njs_vm_t *vm, njs_parser_t *parser, nxt_str_t *name,
    njs_token_t token)
{
    njs_parser_node_t  *node;

    node = parser->node->left;

    if (node->token != NJS_TOKEN_NAME) {
        njs_parser_ref_error(vm, parser, "Invalid left-hand side \"%V\" "
                             "in for-in statement", name);

        return NJS_TOKEN_ILLEGAL;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FOR_IN);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = parser->node;

    token = njs_parser_match(vm, parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_block(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node->right = parser->node;
    parser->node = node;

    return token;
}


static njs_token_t
njs_parser_brk_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    uint32_t           hash;
    njs_ret_t          ret;
    nxt_str_t          name;
    njs_parser_node_t  *node;

    node = njs_parser_node_new(vm, parser, token);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token_line = njs_parser_token_line(parser);
    parser->node = node;

    token = njs_lexer_token(vm, parser->lexer);

    switch (token) {

    case NJS_TOKEN_LINE_END:
        return njs_parser_token(vm, parser);

    case NJS_TOKEN_NAME:
        name = *njs_parser_text(parser);
        hash = njs_parser_key_hash(parser);

        if (njs_label_find(vm, parser->scope, &name, hash) == NULL) {
            njs_parser_syntax_error(vm, parser, "Undefined label \"%V\"",
                                    &name);
            return NJS_TOKEN_ILLEGAL;
        }

        ret = njs_name_copy(vm, &parser->node->label, &name);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        return njs_parser_token(vm, parser);

    case NJS_TOKEN_SEMICOLON:
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
    njs_ret_t          ret;
    njs_token_t        token;
    njs_variable_t     *var;
    njs_parser_node_t  *node, *try, *catch;

    token = njs_parser_try_block(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    try = njs_parser_node_new(vm, parser, NJS_TOKEN_TRY);
    if (nxt_slow_path(try == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    try->left = parser->node;

    if (token == NJS_TOKEN_CATCH) {
        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_match(vm, parser, token, NJS_TOKEN_OPEN_PARENTHESIS);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token != NJS_TOKEN_NAME) {
            return NJS_TOKEN_ILLEGAL;
        }

        catch = njs_parser_node_new(vm, parser, NJS_TOKEN_CATCH);
        if (nxt_slow_path(catch == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        try->right = catch;

        /*
         * The "catch" clause creates a block scope for single variable
         * which receives exception value.
         */
        ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_BLOCK);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        var = njs_parser_variable_add(vm, parser, NJS_VARIABLE_CATCH);
        if (nxt_slow_path(var == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_NAME);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_variable_reference(vm, parser, node, NJS_DECLARATION);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        catch->left = node;

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (nxt_slow_path(token != NJS_TOKEN_CLOSE_PARENTHESIS)) {
            return NJS_TOKEN_ILLEGAL;
        }

        token = njs_parser_try_block(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        catch->right = parser->node;

        njs_parser_scope_end(vm, parser);
    }

    if (token == NJS_TOKEN_FINALLY) {

        token = njs_parser_try_block(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_FINALLY);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->right = parser->node;

        if (try->right != NULL) {
            node->left = try->right;
        }

        try->right = node;
    }

    if (try->right == NULL) {
        njs_parser_syntax_error(vm, parser,
                                "Missing catch or finally after try");

        return NJS_TOKEN_ILLEGAL;
    }

    parser->node = try;

    return token;
}


static njs_token_t
njs_parser_try_block(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token != NJS_TOKEN_OPEN_BRACE)) {
        return NJS_TOKEN_ILLEGAL;
    }

    token = njs_parser_block_statement(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    node = parser->node;

    if (node != NULL && node->token == NJS_TOKEN_BLOCK) {
        parser->node = node->left;

        nxt_mp_free(vm->mem_pool, node);
    }

    return token;
}


static njs_token_t
njs_parser_throw_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t        token;
    njs_parser_node_t  *node;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_THROW);
    if (nxt_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    token = njs_lexer_token(vm, parser->lexer);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    switch (token) {

    case NJS_TOKEN_LINE_END:
        njs_parser_syntax_error(vm, parser, "Illegal newline after throw");
        return NJS_TOKEN_ILLEGAL;

    default:
        token = njs_parser_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        node->right = parser->node;
        parser->node = node;

        return token;
    }
}


static njs_token_t
njs_parser_grouping_expression(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t  token;

    token = njs_parser_token(vm, parser);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_match(vm, parser, token, NJS_TOKEN_OPEN_PARENTHESIS);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    token = njs_parser_expression(vm, parser, token);
    if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
        return token;
    }

    return njs_parser_match(vm, parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
}


njs_token_t
njs_parser_property_token(njs_vm_t *vm, njs_parser_t *parser)
{
    nxt_int_t          ret;
    njs_token_t        token;
    njs_parser_node_t  *node;

    parser->lexer->property = 1;

    token = njs_parser_token(vm, parser);

    parser->lexer->property = 0;

    if (token == NJS_TOKEN_NAME) {
        node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_string_create(vm, &node->u.value);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        parser->node = node;
    }

    return token;
}


njs_token_t
njs_parser_terminal(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    double             num;
    njs_ret_t          ret;
    njs_parser_node_t  *node;

    if (token == NJS_TOKEN_OPEN_PARENTHESIS) {

        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        token = njs_parser_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        return njs_parser_match(vm, parser, token, NJS_TOKEN_CLOSE_PARENTHESIS);
    }

    if (token == NJS_TOKEN_FUNCTION) {
        return njs_parser_function_expression(vm, parser);
    }

    switch (token) {

    case NJS_TOKEN_OPEN_BRACE:
        nxt_thread_log_debug("JS: OBJECT");

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        parser->node = node;

        token = njs_parser_object(vm, parser, node);

        if (parser->node != node) {
            /* The object is not empty. */
            node->left = parser->node;
            parser->node = node;
        }

        return token;

    case NJS_TOKEN_OPEN_BRACKET:
        nxt_thread_log_debug("JS: ARRAY");

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_ARRAY);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        parser->node = node;

        token = njs_parser_array(vm, parser, node);

        if (parser->node != node) {
            /* The array is not empty. */
            node->left = parser->node;
            parser->node = node;
        }

        return token;

    case NJS_TOKEN_DIVISION:
        node = njs_parser_node_new(vm, parser, NJS_TOKEN_REGEXP);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        token = njs_regexp_literal(vm, parser, &node->u.value);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        nxt_thread_log_debug("REGEX: '%V'", njs_parser_text(parser));

        break;

    case NJS_TOKEN_STRING:
        nxt_thread_log_debug("JS: '%V'", njs_parser_text(parser));

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_string_create(vm, &node->u.value);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NJS_TOKEN_ERROR;
        }

        break;

    case NJS_TOKEN_ESCAPE_STRING:
        nxt_thread_log_debug("JS: '%V'", njs_parser_text(parser));

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        ret = njs_parser_escape_string_create(vm, parser, &node->u.value);
        if (nxt_slow_path(ret != NJS_TOKEN_STRING)) {
            return ret;
        }

        break;

    case NJS_TOKEN_UNTERMINATED_STRING:
        njs_parser_syntax_error(vm, parser, "Unterminated string \"%V\"",
                                njs_parser_text(parser));

        return NJS_TOKEN_ILLEGAL;

    case NJS_TOKEN_NUMBER:
        num = njs_parser_number(parser);
        nxt_thread_log_debug("JS: %f", num);

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_NUMBER);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.value.data.u.number = num;
        node->u.value.type = NJS_NUMBER;
        node->u.value.data.truth = njs_is_number_true(num);

        break;

    case NJS_TOKEN_BOOLEAN:
        num = njs_parser_number(parser);
        nxt_thread_log_debug("JS: boolean: %V", njs_parser_text(parser));

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_BOOLEAN);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        if (num == 0) {
            node->u.value = njs_value_false;

        } else {
            node->u.value = njs_value_true;
        }

        break;

    default:
        node = njs_parser_reference(vm, parser, token,
                                    njs_parser_text(parser),
                                    njs_parser_key_hash(parser),
                                    njs_parser_token_line(parser));

        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        break;
    }

    parser->node = node;

    return njs_parser_token(vm, parser);
}


static njs_parser_node_t *
njs_parser_reference(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token,
    nxt_str_t *name, uint32_t hash, uint32_t token_line)
{
    njs_ret_t           ret;
    njs_value_t         *ext;
    njs_parser_node_t   *node;
    njs_parser_scope_t  *scope;

    node = njs_parser_node_new(vm, parser, token);
    if (nxt_slow_path(node == NULL)) {
        return NULL;
    }

    switch (token) {

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

        scope = parser->scope;

        while (scope->type != NJS_SCOPE_GLOBAL) {
            if (scope->type == NJS_SCOPE_FUNCTION) {
                node->index = NJS_INDEX_THIS;
                break;
            }

            scope = scope->parent;
        }

        if (node->index == NJS_INDEX_THIS) {
            break;
        }

        node->token = NJS_TOKEN_GLOBAL_THIS;

        /* Fall through. */

    case NJS_TOKEN_NJS:
    case NJS_TOKEN_MATH:
    case NJS_TOKEN_JSON:
        ret = njs_parser_builtin(vm, parser, node, NJS_OBJECT, name, hash);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        break;

    case NJS_TOKEN_ARGUMENTS:
        nxt_thread_log_debug("JS: arguments");

        if (parser->scope->type <= NJS_SCOPE_GLOBAL) {
            njs_parser_syntax_error(vm, parser, "\"%V\" object "
                                    "in global scope", name);

            return NULL;
        }

        parser->scope->arguments_object = 1;

        break;

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

    case NJS_TOKEN_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_ERROR;
        break;

    case NJS_TOKEN_EVAL_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_EVAL_ERROR;
        break;

    case NJS_TOKEN_INTERNAL_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_INTERNAL_ERROR;
        break;

    case NJS_TOKEN_RANGE_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_RANGE_ERROR;
        break;

    case NJS_TOKEN_REF_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_REF_ERROR;
        break;

    case NJS_TOKEN_SYNTAX_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_SYNTAX_ERROR;
        break;

    case NJS_TOKEN_TYPE_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_TYPE_ERROR;
        break;

    case NJS_TOKEN_URI_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_URI_ERROR;
        break;

    case NJS_TOKEN_MEMORY_ERROR_CONSTRUCTOR:
        node->index = NJS_INDEX_OBJECT_MEMORY_ERROR;
        break;

    case NJS_TOKEN_EVAL:
    case NJS_TOKEN_TO_STRING:
    case NJS_TOKEN_IS_NAN:
    case NJS_TOKEN_IS_FINITE:
    case NJS_TOKEN_PARSE_INT:
    case NJS_TOKEN_PARSE_FLOAT:
    case NJS_TOKEN_ENCODE_URI:
    case NJS_TOKEN_ENCODE_URI_COMPONENT:
    case NJS_TOKEN_DECODE_URI:
    case NJS_TOKEN_DECODE_URI_COMPONENT:
    case NJS_TOKEN_REQUIRE:
    case NJS_TOKEN_SET_TIMEOUT:
    case NJS_TOKEN_SET_IMMEDIATE:
    case NJS_TOKEN_CLEAR_TIMEOUT:
        ret = njs_parser_builtin(vm, parser, node, NJS_FUNCTION, name, hash);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        break;

    case NJS_TOKEN_NAME:
        nxt_thread_log_debug("JS: %V", name);

        node->token_line = token_line;

        ext = njs_external_lookup(vm, name, hash);

        if (ext != NULL) {
            node->token = NJS_TOKEN_EXTERNAL;
            node->u.value = *ext;
            node->index = (njs_index_t) ext;
            break;
        }

        ret = njs_variable_reference(vm, parser->scope, node, name, hash,
                                     NJS_REFERENCE);
        if (nxt_slow_path(ret != NXT_OK)) {
            return NULL;
        }

        break;

    default:
        (void) njs_parser_unexpected_token(vm, parser, token);
        return NULL;
    }

    return node;
}


static nxt_int_t
njs_parser_builtin(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *node,
    njs_value_type_t type, nxt_str_t *name, uint32_t hash)
{
    njs_ret_t           ret;
    nxt_uint_t          index;
    njs_variable_t      *var;
    njs_parser_scope_t  *scope;

    scope = njs_parser_global_scope(vm);

    var = njs_variable_add(vm, scope, name, hash, NJS_VARIABLE_VAR);
    if (nxt_slow_path(var == NULL)) {
        return NXT_ERROR;
    }

    /* TODO: once */
    switch (type) {
    case NJS_OBJECT:
        index = node->token - NJS_TOKEN_FIRST_OBJECT;
        var->value.data.u.object = &vm->shared->objects[index];
        break;

    case NJS_FUNCTION:
        index = node->token - NJS_TOKEN_FIRST_FUNCTION;
        var->value.data.u.function = &vm->shared->functions[index];
        break;

    default:
        return NXT_ERROR;
    }

    var->value.type = type;
    var->value.data.truth = 1;

    ret = njs_variable_reference(vm, scope, node, name, hash, NJS_REFERENCE);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


/*
 * ES6: 12.2.6 Object Initializer
 * Supported syntax:
 *   PropertyDefinition:
 *     PropertyName : AssignmentExpression
 *     IdentifierReference
 *   PropertyName:
 *    IdentifierName, StringLiteral, NumericLiteral.
 */
static njs_token_t
njs_parser_object(njs_vm_t *vm, njs_parser_t *parser, njs_parser_node_t *obj)
{
    uint32_t           hash, token_line;
    nxt_str_t          name;
    njs_token_t        token;
    njs_lexer_t        *lexer;
    njs_parser_node_t  *stmt, *assign, *object, *propref, *left, *expression;

    left = NULL;
    lexer = parser->lexer;

    /* GCC and Clang complain about uninitialized hash. */
    hash = 0;
    token_line = 0;

    object = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT_VALUE);
    if (nxt_slow_path(object == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    object->u.object = obj;

    for ( ;; ) {
        token = njs_parser_property_token(vm, parser);

        name.start = NULL;

        switch (token) {

        case NJS_TOKEN_CLOSE_BRACE:
            return njs_parser_token(vm, parser);

        case NJS_TOKEN_NAME:
            name = *njs_parser_text(parser);

            hash = njs_parser_key_hash(parser);
            token_line = njs_parser_token_line(parser);

            token = njs_parser_token(vm, parser);
            break;

        case NJS_TOKEN_NUMBER:
        case NJS_TOKEN_STRING:
        case NJS_TOKEN_ESCAPE_STRING:
            token = njs_parser_terminal(vm, parser, token);
            break;

        default:
            return NJS_TOKEN_ILLEGAL;
        }

        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        propref = njs_parser_node_new(vm, parser, NJS_TOKEN_PROPERTY);
        if (nxt_slow_path(propref == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        propref->left = object;
        propref->right = parser->node;

        if (name.start != NULL
            && (token == NJS_TOKEN_COMMA || token == NJS_TOKEN_CLOSE_BRACE)
            && lexer->property_token != NJS_TOKEN_THIS
            && lexer->property_token != NJS_TOKEN_GLOBAL_THIS)
        {
            expression = njs_parser_reference(vm, parser, lexer->property_token,
                                              &name, hash, token_line);
            if (nxt_slow_path(expression == NULL)) {
                return NJS_TOKEN_ERROR;
            }

        } else {
            token = njs_parser_match(vm, parser, token, NJS_TOKEN_COLON);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            token = njs_parser_assignment_expression(vm, parser, token);
            if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
                return token;
            }

            expression = parser->node;
        }

        assign = njs_parser_node_new(vm, parser, NJS_TOKEN_ASSIGNMENT);
        if (nxt_slow_path(assign == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        assign->u.operation = njs_vmcode_move;
        assign->left = propref;
        assign->right = expression;

        stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
        if (nxt_slow_path(stmt == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        stmt->left = left;
        stmt->right = assign;

        parser->node = stmt;

        left = stmt;

        if (token == NJS_TOKEN_CLOSE_BRACE) {
            return njs_parser_token(vm, parser);
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
        token = njs_parser_token(vm, parser);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        if (token == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (token == NJS_TOKEN_COMMA) {
            obj->ctor = 1;
            index++;
            continue;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_NUMBER);
        if (nxt_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->u.value.data.u.number = index;
        node->u.value.type = NJS_NUMBER;
        node->u.value.data.truth = (index != 0);
        index++;

        object = njs_parser_node_new(vm, parser, NJS_TOKEN_OBJECT_VALUE);
        if (nxt_slow_path(object == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        object->u.object = obj;

        propref = njs_parser_node_new(vm, parser, NJS_TOKEN_PROPERTY);
        if (nxt_slow_path(propref == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        propref->left = object;
        propref->right = node;

        token = njs_parser_assignment_expression(vm, parser, token);
        if (nxt_slow_path(token <= NJS_TOKEN_ILLEGAL)) {
            return token;
        }

        assign = njs_parser_node_new(vm, parser, NJS_TOKEN_ASSIGNMENT);
        if (nxt_slow_path(assign == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        assign->u.operation = njs_vmcode_move;
        assign->left = propref;
        assign->right = parser->node;

        stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
        if (nxt_slow_path(stmt == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        stmt->left = left;
        stmt->right = assign;

        parser->node = stmt;
        left = stmt;

        obj->ctor = 0;

        if (token == NJS_TOKEN_CLOSE_BRACKET) {
            break;
        }

        if (nxt_slow_path(token != NJS_TOKEN_COMMA)) {
            return NJS_TOKEN_ILLEGAL;
        }
    }

    obj->u.length = index;

    return njs_parser_token(vm, parser);
}


static nxt_int_t
njs_parser_string_create(njs_vm_t *vm, njs_value_t *value)
{
    u_char     *p;
    ssize_t    length;
    nxt_str_t  *src;

    src = njs_parser_text(vm->parser);

    length = nxt_utf8_length(src->start, src->length);

    if (nxt_slow_path(length < 0)) {
        length = 0;
    }

    p = njs_string_alloc(vm, value, src->length, length);

    if (nxt_fast_path(p != NULL)) {
        memcpy(p, src->start, src->length);

        if (length > NJS_STRING_MAP_STRIDE && (size_t) length != src->length) {
            njs_string_offset_map_init(p, src->length);
        }

        return NXT_OK;
    }

    return NXT_ERROR;
}


static njs_token_t
njs_parser_escape_string_create(njs_vm_t *vm, njs_parser_t *parser,
    njs_value_t *value)
{
    u_char        c, *start, *dst;
    size_t        size,length, hex_length;
    uint64_t      u;
    nxt_str_t     *string;
    const u_char  *p, *src, *end, *hex_end;

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

        string = njs_parser_text(parser);
        src = string->start;
        end = src + string->length;

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
                    hex_length = 4;
                    /*
                     * A character after "u" can be safely tested here
                     * because there is always a closing quote at the
                     * end of string: ...\u".
                     */
                    if (*src != '{') {
                        goto hex_length_test;
                    }

                    src++;
                    hex_length = 0;
                    hex_end = end;

                    goto hex;

                case 'x':
                    hex_length = 2;
                    goto hex_length_test;

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

        hex_length_test:

            hex_end = src + hex_length;

            if (hex_end > end) {
                goto invalid;
            }

        hex:

            p = src;
            u = njs_number_hex_parse(&src, hex_end);

            if (hex_length != 0) {
                if (src != hex_end) {
                    goto invalid;
                }

            } else {
                if (src == p || (src - p) > 6) {
                    goto invalid;
                }

                if (src == end || *src++ != '}') {
                    goto invalid;
                }
            }

            size += nxt_utf8_size(u);
            length++;

            if (dst != NULL) {
                dst = nxt_utf8_encode(dst, (uint32_t) u);
                if (dst == NULL) {
                    goto invalid;
                }
            }
        }

        if (start != NULL) {
            if (length > NJS_STRING_MAP_STRIDE && length != size) {
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

invalid:

    njs_parser_syntax_error(vm, parser, "Invalid Unicode code point \"%V\"",
                            njs_parser_text(parser));

    return NJS_TOKEN_ILLEGAL;
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


static njs_token_t
njs_parser_unexpected_token(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token)
{
    if (token != NJS_TOKEN_END) {
        njs_parser_syntax_error(vm, parser, "Unexpected token \"%V\"",
                                njs_parser_text(parser));

    } else {
        njs_parser_syntax_error(vm, parser, "Unexpected end of input");
    }

    return NJS_TOKEN_ILLEGAL;
}


u_char *
njs_parser_trace_handler(nxt_trace_t *trace, nxt_trace_data_t *td,
    u_char *start)
{
    u_char        *p;
    size_t        size;
    njs_vm_t      *vm;
    njs_lexer_t   *lexer;
    njs_parser_t  *parser;

    size = nxt_length("InternalError: ");
    memcpy(start, "InternalError: ", size);
    p = start + size;

    vm = trace->data;

    trace = trace->next;
    p = trace->handler(trace, td, p);

    parser = vm->parser;

    if (parser != NULL && parser->lexer != NULL) {
        lexer = parser->lexer;

        if (lexer->file.length != 0) {
            njs_internal_error(vm, "%s in %V:%uD", start, &lexer->file,
                               njs_parser_token_line(parser));
        } else {
            njs_internal_error(vm, "%s in %uD", start,
                               njs_parser_token_line(parser));
        }

    } else {
        njs_internal_error(vm, "%s", start);
    }

    return p;
}


static void
njs_parser_scope_error(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_value_type_t type, uint32_t line, const char *fmt, va_list args)
{
    size_t     width;
    u_char     msg[NXT_MAX_ERROR_STR];
    u_char     *p, *end;
    nxt_str_t  *file;

    file = &scope->file;

    p = msg;
    end = msg + NXT_MAX_ERROR_STR;

    p = nxt_vsprintf(p, end, fmt, args);

    width = nxt_length(" in ") + file->length + NXT_INT_T_LEN;

    if (p > end - width) {
        p = end - width;
    }

    if (file->length != 0) {
        p = nxt_sprintf(p, end, " in %V:%uD", file, line);

    } else {
        p = nxt_sprintf(p, end, " in %uD", line);
    }

    njs_error_new(vm, &vm->retval, type, msg, p - msg);
}


void
njs_parser_lexer_error(njs_vm_t *vm, njs_parser_t *parser,
    njs_value_type_t type, const char *fmt, ...)
{
    va_list  args;

    if (njs_is_error(&vm->retval)) {
        return;
    }

    va_start(args, fmt);
    njs_parser_scope_error(vm, parser->scope, type, parser->lexer->line, fmt,
                           args);
    va_end(args);
}


void
njs_parser_node_error(njs_vm_t *vm, njs_parser_node_t *node,
    njs_value_type_t type, const char *fmt, ...)
{
    va_list  args;

    va_start(args, fmt);
    njs_parser_scope_error(vm, node->scope, type, node->token_line, fmt, args);
    va_end(args);
}
