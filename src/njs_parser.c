
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t njs_parser_scope_begin(njs_vm_t *vm, njs_parser_t *parser,
    njs_scope_t type);
static void njs_parser_scope_end(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_type_t njs_parser_statement_chain(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_type_t type, njs_bool_t top);
static njs_token_type_t njs_parser_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type);
static njs_token_type_t njs_parser_block_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_block(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_type_t type);
static njs_token_type_t njs_parser_labelled_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_function_declaration(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_lambda_arguments(njs_vm_t *vm,
    njs_parser_t *parser, njs_function_lambda_t *lambda, njs_index_t index,
    njs_token_type_t type);
static njs_token_type_t njs_parser_lambda_argument(njs_vm_t *vm,
    njs_parser_t *parser, njs_index_t index);
static njs_token_type_t njs_parser_lambda_body(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_type_t type);
static njs_parser_node_t *njs_parser_return_set(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *expr);
static njs_token_type_t njs_parser_return_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_var_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_type_t parent, njs_bool_t var_in);
static njs_token_type_t njs_parser_if_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_switch_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_while_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_do_while_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_for_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_var_in_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_parser_node_t *name);
static njs_token_type_t njs_parser_for_in_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_str_t *name, njs_token_type_t type);
static njs_token_type_t njs_parser_brk_statement(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_type_t type);
static njs_token_type_t njs_parser_try_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_try_block(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_throw_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_import_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_token_type_t njs_parser_export_statement(njs_vm_t *vm,
    njs_parser_t *parser);
static njs_int_t njs_parser_export_sink(njs_vm_t *vm, njs_parser_t *parser);
static njs_token_type_t njs_parser_grouping_expression(njs_vm_t *vm,
    njs_parser_t *parser);


#define njs_parser_chain_current(parser)                            \
    ((parser)->node)

#define njs_parser_chain_top(parser)                                \
    ((parser)->scope->top)

#define njs_parser_chain_top_set(parser, node)                      \
    (parser)->scope->top = node


njs_int_t
njs_parser(njs_vm_t *vm, njs_parser_t *parser, njs_parser_t *prev)
{
    njs_int_t          ret;
    njs_token_type_t   type;
    njs_parser_node_t  *node;

    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_GLOBAL);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (prev != NULL) {
        /*
         * Copy the global scope variables from the previous
         * iteration of the accumulative mode.
         */
        ret = njs_variables_copy(vm, &parser->scope->variables,
                                 &prev->scope->variables);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    type = njs_parser_token(vm, parser);

    while (type != NJS_TOKEN_END) {

        type = njs_parser_statement_chain(vm, parser, type, 1);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return NJS_ERROR;
        }

        if (type == NJS_TOKEN_CLOSE_BRACE && vm->options.trailer) {
            parser->lexer->start--;
            break;
        }
    }

    node = njs_parser_chain_top(parser);

    if (node == NULL) {
        /* Empty string, just semicolons or variables declarations. */

        node = njs_parser_node_new(vm, parser, 0);
        if (njs_slow_path(node == NULL)) {
            return NJS_ERROR;
        }

        njs_parser_chain_top_set(parser, node);
    }

    node->token_type = NJS_TOKEN_END;

    if (njs_slow_path(parser->count != 0)) {
        njs_internal_error(vm, "parser->count != 0");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_parser_scope_begin(njs_vm_t *vm, njs_parser_t *parser, njs_scope_t type)
{
    njs_arr_t           *values;
    njs_uint_t          nesting;
    njs_lexer_t         *lexer;
    njs_parser_scope_t  *scope, *parent;

    nesting = 0;

    if (type == NJS_SCOPE_FUNCTION) {

        for (scope = parser->scope; scope != NULL; scope = scope->parent) {

            if (scope->type == NJS_SCOPE_FUNCTION) {
                nesting = scope->nesting + 1;

                if (nesting < NJS_MAX_NESTING) {
                    break;
                }

                njs_parser_syntax_error(vm, parser,
                                        "The maximum function nesting "
                                        "level is \"%d\"", NJS_MAX_NESTING);

                return NJS_ERROR;
            }
        }
    }

    scope = njs_mp_zalloc(vm->mem_pool, sizeof(njs_parser_scope_t));
    if (njs_slow_path(scope == NULL)) {
        return NJS_ERROR;
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

    njs_queue_init(&scope->nested);
    njs_rbtree_init(&scope->variables, njs_parser_scope_rbtree_compare);
    njs_rbtree_init(&scope->labels, njs_parser_scope_rbtree_compare);
    njs_rbtree_init(&scope->references, njs_parser_scope_rbtree_compare);

    values = NULL;

    if (scope->type < NJS_SCOPE_BLOCK) {
        values = njs_arr_create(vm->mem_pool, 4, sizeof(njs_value_t));
        if (njs_slow_path(values == NULL)) {
            return NJS_ERROR;
        }
    }

    scope->values[0] = values;
    scope->values[1] = NULL;

    lexer = parser->lexer;

    if (lexer->file.length != 0) {
        njs_file_basename(&lexer->file, &scope->file);
        njs_file_dirname(&lexer->file, &scope->cwd);
    }

    parent = parser->scope;
    scope->parent = parent;
    parser->scope = scope;

    if (parent != NULL) {
        njs_queue_insert_tail(&parent->nested, &scope->link);

        if (nesting == 0) {
            /* Inherit function nesting in blocks. */
            scope->nesting = parent->nesting;
        }
    }

    return NJS_OK;
}


static void
njs_parser_scope_end(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_parser_scope_t  *scope, *parent;

    scope = parser->scope;

    parent = scope->parent;
    parser->scope = parent;
}


intptr_t
njs_parser_scope_rbtree_compare(njs_rbtree_node_t *node1,
    njs_rbtree_node_t *node2)
{
    njs_variable_node_t  *lex_node1, *lex_node2;

    lex_node1 = (njs_variable_node_t *) node1;
    lex_node2 = (njs_variable_node_t *) node2;

    if (lex_node1->key < lex_node2->key) {
        return -1;
    }

    if (lex_node1->key > lex_node2->key) {
        return 1;
    }

    return 0;
}


static njs_token_type_t
njs_parser_statement_chain(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type, njs_bool_t top)
{
    njs_parser_node_t  *stmt, *last, *node, *new_node, **child;

    child = top ? &njs_parser_chain_top(parser)
                : &njs_parser_chain_current(parser);

    last = *child;

    njs_parser_enter(vm, parser);

    type = njs_parser_statement(vm, parser, type);

    njs_parser_leave(parser);

    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return njs_parser_unexpected_token(vm, parser, type);
    }

    if (parser->node == NULL) {
        /* The statement is empty block or just semicolon. */
        return type;
    }

    new_node = parser->node;

    if (new_node->hoist) {
        child = &njs_parser_chain_top(parser);

        while (*child != NULL) {
            node = *child;

            if (node->hoist) {
                break;
            }

            child = &node->left;
        }

        last = *child;
    }

    stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
    if (njs_slow_path(stmt == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    stmt->hoist = new_node->hoist;
    stmt->left = last;
    stmt->right = new_node;

    *child = stmt;

    while (type == NJS_TOKEN_SEMICOLON) {
        type = njs_parser_token(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            break;
        }
    }

    return type;
}


static njs_token_type_t
njs_parser_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type)
{
    size_t  offset;

    parser->node = NULL;

    switch (type) {

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
            njs_thread_log_debug("BLOCK END");
            return type;
        }

        /* Fall through. */

    default:

        switch (type) {
        case NJS_TOKEN_VAR:
            type = njs_parser_var_statement(vm, parser, type, 0);
            break;

        case NJS_TOKEN_RETURN:
            type = njs_parser_return_statement(vm, parser);
            break;

        case NJS_TOKEN_THROW:
            type = njs_parser_throw_statement(vm, parser);
            break;

        case NJS_TOKEN_CONTINUE:
        case NJS_TOKEN_BREAK:
            type = njs_parser_brk_statement(vm, parser, type);
            break;

        case NJS_TOKEN_IMPORT:
            type = njs_parser_import_statement(vm, parser);
            break;

        case NJS_TOKEN_EXPORT:
            type = njs_parser_export_statement(vm, parser);
            break;

        case NJS_TOKEN_NAME:
            offset = 0;
            if (njs_parser_peek_token(vm, parser, &offset) == NJS_TOKEN_COLON) {
                return njs_parser_labelled_statement(vm, parser);
            }

            /* Fall through. */

        default:
            type = njs_parser_expression(vm, parser, type);
            break;
        }

        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        /*
         * An expression must be terminated by semicolon,
         * or by a close curly brace or by the end of line.
         */
        switch (type) {

        case NJS_TOKEN_SEMICOLON:
            return njs_parser_token(vm, parser);

        case NJS_TOKEN_CLOSE_BRACE:
        case NJS_TOKEN_END:
            return type;

        default:
            if (parser->lexer->prev_type == NJS_TOKEN_LINE_END) {
                return type;
            }

            return NJS_TOKEN_ILLEGAL;
        }
    }
}


static njs_token_type_t
njs_parser_block_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_int_t          ret;
    njs_token_type_t   type;
    njs_parser_node_t  *node;

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_BLOCK);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_TOKEN_ERROR;
    }

    parser->node = NULL;

    while (type != NJS_TOKEN_CLOSE_BRACE) {
        type = njs_parser_statement_chain(vm, parser, type, 0);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_BLOCK);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = parser->node;
    node->right = NULL;
    parser->node = node;

    njs_parser_scope_end(vm, parser);

    return njs_parser_token(vm, parser);
}


static njs_token_type_t
njs_parser_block(njs_vm_t *vm, njs_parser_t *parser, njs_token_type_t type)
{
    if (type == NJS_TOKEN_FUNCTION) {
        njs_parser_syntax_error(vm, parser,
              "Functions can only be declared at top level or inside a block");
        return NJS_TOKEN_ILLEGAL;
    }

    return njs_parser_statement(vm, parser, type);
}


static njs_parser_node_t *
njs_parser_variable_node(njs_vm_t *vm, njs_parser_t *parser,
    uintptr_t unique_id, njs_variable_type_t type)
{
    njs_int_t          ret;
    njs_variable_t     *var;
    njs_parser_node_t  *node;

    var = njs_variable_add(vm, parser->scope, unique_id, type);
    if (njs_slow_path(var == NULL)) {
        return NULL;
    }

    if (njs_is_null(&var->value)) {

        switch (type) {

        case NJS_VARIABLE_VAR:
            njs_set_undefined(&var->value);
            break;

        default:
            break;
        }
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_NAME);
    if (njs_slow_path(node == NULL)) {
        return NULL;
    }

    ret = njs_variable_reference(vm, parser->scope, node, unique_id,
                                 NJS_DECLARATION);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return node;
}


static njs_token_type_t
njs_parser_labelled_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    uintptr_t         unique_id;
    njs_int_t         ret;
    njs_str_t         name;
    njs_variable_t    *label;
    njs_token_type_t  type;

    name = *njs_parser_text(parser);
    unique_id = njs_parser_key_hash(parser);

    label = njs_label_find(vm, parser->scope, unique_id);
    if (njs_slow_path(label != NULL)) {
        njs_parser_syntax_error(vm, parser, "Label \"%V\" "
                                "has already been declared", &name);
        return NJS_TOKEN_ILLEGAL;
    }

    label = njs_label_add(vm, parser->scope, unique_id);
    if (njs_slow_path(label == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_COLON);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_statement(vm, parser, type);

    if (njs_fast_path(type > NJS_TOKEN_ILLEGAL)) {

        if (parser->node != NULL) {
            /* The statement is not empty block or just semicolon. */

            ret = njs_name_copy(vm, &parser->node->name, &name);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_TOKEN_ERROR;
            }

            ret = njs_label_remove(vm, parser->scope, unique_id);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_TOKEN_ERROR;
            }
        }
    }

    return type;
}


static njs_function_t *
njs_parser_function_alloc(njs_vm_t *vm, njs_parser_t *parser,
    njs_variable_t *var)
{
    njs_value_t            *value;
    njs_function_t         *function;
    njs_function_lambda_t  *lambda;

    lambda = njs_function_lambda_alloc(vm, 1);
    if (njs_slow_path(lambda == NULL)) {
        njs_memory_error(vm);
        return NULL;
    }

    /* TODO:
     *  njs_function_t is used to pass lambda to
     *  njs_generate_function_declaration() and is not actually needed.
     *  real njs_function_t is created by njs_vmcode_function() in runtime.
     */

    function = njs_function_alloc(vm, lambda, NULL, 1);
    if (njs_slow_path(function == NULL)) {
        return NULL;
    }

    njs_set_function(&var->value, function);

    if (var->index != NJS_INDEX_NONE
        && njs_scope_accumulative(vm, parser->scope))
    {
        value = (njs_value_t *) var->index;
        *value = var->value;
    }

    return function;
}


static njs_token_type_t
njs_parser_function_declaration(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_int_t          ret;
    njs_variable_t     *var;
    njs_function_t     *function;
    njs_token_type_t   type;
    njs_parser_node_t  *node;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FUNCTION);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token_line = njs_parser_token_line(parser);

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    if (type != NJS_TOKEN_NAME) {
        if (njs_parser_restricted_identifier(type)) {
            njs_parser_syntax_error(vm, parser, "Identifier \"%V\" "
                                    "is forbidden in function declaration",
                                    njs_parser_text(parser));
        }

        return NJS_TOKEN_ILLEGAL;
    }

    var = njs_parser_variable_add(vm, parser, NJS_VARIABLE_FUNCTION);
    if (njs_slow_path(var == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    ret = njs_parser_variable_reference(vm, parser, node, NJS_DECLARATION);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_TOKEN_ERROR;
    }

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    parser->node = node;

    function = njs_parser_function_alloc(vm, parser, var);
    if (njs_slow_path(function == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    type = njs_parser_function_lambda(vm, parser, function->u.lambda, type);

    function->args_count = function->u.lambda->nargs
                           - function->u.lambda->rest_parameters;

    return type;
}


njs_token_type_t
njs_parser_function_expression(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_int_t              ret;
    njs_variable_t         *var;
    njs_function_t         *function;
    njs_token_type_t       type;
    njs_parser_node_t      *node;
    njs_function_lambda_t  *lambda;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FUNCTION_EXPRESSION);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token_line = njs_parser_token_line(parser);
    parser->node = node;

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    /*
     * An optional function expression name is stored
     * in intermediate shim scope.
     */
    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_SHIM);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_TOKEN_ERROR;
    }

    if (type == NJS_TOKEN_NAME) {
        var = njs_parser_variable_add(vm, parser, NJS_VARIABLE_SHIM);
        if (njs_slow_path(var == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        type = njs_parser_token(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        function = njs_parser_function_alloc(vm, parser, var);
        if (njs_slow_path(function == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        lambda = function->u.lambda;

    } else {
        /* Anonymous function. */
        lambda = njs_function_lambda_alloc(vm, 1);
        if (njs_slow_path(lambda == NULL)) {
            return NJS_TOKEN_ERROR;
        }
    }

    node->u.value.data.u.lambda = lambda;

    type = njs_parser_function_lambda(vm, parser, lambda, type);

    njs_parser_scope_end(vm, parser);

    return type;
}


njs_token_type_t
njs_parser_function_lambda(njs_vm_t *vm, njs_parser_t *parser,
    njs_function_lambda_t *lambda, njs_token_type_t type)
{
    njs_int_t    ret;
    njs_index_t  index;

    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_FUNCTION);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_TOKEN_ERROR;
    }

    index = NJS_SCOPE_ARGUMENTS;

    /* A "this" reservation. */
    index += sizeof(njs_value_t);

    type = njs_parser_lambda_arguments(vm, parser, lambda, index, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_lambda_body(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    njs_parser_scope_end(vm, parser);

    return type;
}


static njs_token_type_t
njs_parser_lambda_arguments(njs_vm_t *vm, njs_parser_t *parser,
    njs_function_lambda_t *lambda, njs_index_t index, njs_token_type_t type)
{
    type = njs_parser_match(vm, parser, type, NJS_TOKEN_OPEN_PARENTHESIS);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    lambda->nargs = 0;

    while (type != NJS_TOKEN_CLOSE_PARENTHESIS) {

        if (njs_slow_path(lambda->rest_parameters)) {
            return NJS_TOKEN_ILLEGAL;
        }

        if (njs_slow_path(type == NJS_TOKEN_ELLIPSIS)) {
            lambda->rest_parameters = 1;

            type = njs_parser_token(vm, parser);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                return NJS_TOKEN_ILLEGAL;
            }
        }

        if (njs_slow_path(type != NJS_TOKEN_NAME)) {
            return NJS_TOKEN_ILLEGAL;
        }

        type = njs_parser_lambda_argument(vm, parser, index);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        if (type == NJS_TOKEN_COMMA) {
            type = njs_parser_token(vm, parser);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                return type;
            }
        }

        lambda->nargs++;
        index += sizeof(njs_value_t);
    }

    return njs_parser_token(vm, parser);
}


static njs_token_type_t
njs_parser_lambda_argument(njs_vm_t *vm, njs_parser_t *parser,
    njs_index_t index)
{
    njs_variable_t  *arg;

    arg = njs_parser_variable_add(vm, parser, NJS_VARIABLE_VAR);
    if (njs_slow_path(arg == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    if (arg->index > 0) {
        njs_parser_syntax_error(vm, parser, "Duplicate parameter names");

        return NJS_TOKEN_ILLEGAL;
    }

    arg->index = index;
    arg->unique_id = njs_parser_key_hash(parser);

    return njs_parser_token(vm, parser);
}


static njs_token_type_t
njs_parser_lambda_body(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type)
{
    njs_parser_node_t  *body, *last, *parent;

    parent = parser->node;

    type = njs_parser_lambda_statements(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
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

    if (last == NULL || last->token_type != NJS_TOKEN_RETURN) {
        /*
         * There is no function body or the last function body
         * body statement is not "return" statement.
         */
        body = njs_parser_return_set(vm, parser, NULL);
        if (njs_slow_path(body == NULL)) {
            return NJS_TOKEN_ERROR;
        }
    }

    parent->right = body;

    parser->node = parent;

    return type;
}


static njs_parser_node_t *
njs_parser_return_set(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *expr)
{
    njs_parser_node_t  *stmt, *node;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_RETURN);
    if (njs_slow_path(node == NULL)) {
        return NULL;
    }

    node->right = expr;

    stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
    if (njs_slow_path(stmt == NULL)) {
        return NULL;
    }

    stmt->left = njs_parser_chain_top(parser);
    stmt->right = node;

    njs_parser_chain_top_set(parser, stmt);

    return stmt;
}


njs_token_type_t
njs_parser_lambda_statements(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type)
{
    type = njs_parser_match(vm, parser, type, NJS_TOKEN_OPEN_BRACE);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    parser->node = NULL;

    while (type != NJS_TOKEN_CLOSE_BRACE) {
        type = njs_parser_statement_chain(vm, parser, type, 1);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }
    }

    return njs_parser_token(vm, parser);
}


static njs_token_type_t
njs_parser_return_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t    type;
    njs_parser_node_t   *node;
    njs_parser_scope_t  *scope;

    for (scope = parser->scope;
         scope != NULL;
         scope = scope->parent)
    {
        if (scope->type == NJS_SCOPE_FUNCTION && !scope->module) {
            break;
        }

        if (scope->type == NJS_SCOPE_GLOBAL) {
            njs_parser_syntax_error(vm, parser, "Illegal return statement");

            return NJS_ERROR;
        }
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_RETURN);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    parser->node = node;

    type = njs_lexer_token(vm, parser->lexer);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    switch (type) {

    case NJS_TOKEN_LINE_END:
        return njs_parser_token(vm, parser);

    case NJS_TOKEN_SEMICOLON:
    case NJS_TOKEN_CLOSE_BRACE:
    case NJS_TOKEN_END:
        return type;

    default:
        type = njs_parser_expression(vm, parser, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        if (parser->node->token_type == NJS_TOKEN_FUNCTION) {
            /* TODO: test closure */
        }

        node->right = parser->node;
        parser->node = node;

        return type;
    }
}


static njs_token_type_t
njs_parser_var_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t parent, njs_bool_t var_in)
{
    njs_token_type_t     token_type;
    njs_parser_node_t    *left, *stmt, *name, *assign, *expr;
    njs_variable_type_t  type;

    type = NJS_VARIABLE_VAR;

    parser->node = NULL;
    left = NULL;

    do {
        token_type = njs_parser_token(vm, parser);
        if (njs_slow_path(token_type <= NJS_TOKEN_ILLEGAL)) {
            return token_type;
        }

        if (token_type != NJS_TOKEN_NAME) {
            if (njs_parser_restricted_identifier(token_type)) {
                njs_parser_syntax_error(vm, parser, "Identifier \"%V\" "
                                        "is forbidden in var declaration",
                                        njs_parser_text(parser));
            }

            return NJS_TOKEN_ILLEGAL;
        }

        name = njs_parser_variable_node(vm, parser, njs_parser_key_hash(parser),
                                        type);
        if (njs_slow_path(name == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        token_type = njs_parser_token(vm, parser);
        if (njs_slow_path(token_type <= NJS_TOKEN_ILLEGAL)) {
            return token_type;
        }

        if (var_in) {
            if (token_type == NJS_TOKEN_IN) {
                return njs_parser_var_in_statement(vm, parser, name);
            }

            var_in = 0;
        }

        expr = NULL;

        if (token_type == NJS_TOKEN_ASSIGNMENT) {

            token_type = njs_parser_token(vm, parser);
            if (njs_slow_path(token_type <= NJS_TOKEN_ILLEGAL)) {
                return token_type;
            }

            token_type = njs_parser_assignment_expression(vm, parser,
                                                          token_type);
            if (njs_slow_path(token_type <= NJS_TOKEN_ILLEGAL)) {
                return token_type;
            }

            expr = parser->node;
        }

        assign = njs_parser_node_new(vm, parser, parent);
        if (njs_slow_path(assign == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        assign->u.operation = NJS_VMCODE_MOVE;
        assign->left = name;
        assign->right = expr;

        stmt = njs_parser_node_new(vm, parser, NJS_TOKEN_STATEMENT);
        if (njs_slow_path(stmt == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        stmt->left = left;
        stmt->right = assign;
        parser->node = stmt;

        left = stmt;

    } while (token_type == NJS_TOKEN_COMMA);

    return token_type;
}


static njs_token_type_t
njs_parser_if_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t   type;
    njs_parser_node_t  *node, *cond, *stmt;

    type = njs_parser_grouping_expression(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    cond = parser->node;

    type = njs_parser_block(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    if (type == NJS_TOKEN_ELSE) {

        stmt = parser->node;

        type = njs_parser_token(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        type = njs_parser_block(vm, parser, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_BRANCHING);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        node->left = stmt;
        node->right = parser->node;
        parser->node = node;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_IF);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = cond;
    node->right = parser->node;
    parser->node = node;

    return type;
}


static njs_token_type_t
njs_parser_switch_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t   type;
    njs_parser_node_t  *node, *swtch, *branch, *dflt, **last;

    node = NULL;
    branch = NULL;
    dflt = NULL;

    type = njs_parser_grouping_expression(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    swtch = njs_parser_node_new(vm, parser, NJS_TOKEN_SWITCH);
    if (njs_slow_path(swtch == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    swtch->left = parser->node;
    last = &swtch->right;

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_OPEN_BRACE);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    while (type != NJS_TOKEN_CLOSE_BRACE) {

        if (type == NJS_TOKEN_CASE || type == NJS_TOKEN_DEFAULT) {

            do {
                node = njs_parser_node_new(vm, parser, 0);
                if (njs_slow_path(node == NULL)) {
                    return NJS_TOKEN_ERROR;
                }

                if (type == NJS_TOKEN_CASE) {
                    type = njs_parser_token(vm, parser);
                    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                        return type;
                    }

                    type = njs_parser_expression(vm, parser, type);
                    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                        return type;
                    }

                    node->left = parser->node;

                    branch = njs_parser_node_new(vm, parser, 0);
                    if (njs_slow_path(branch == NULL)) {
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
                    branch->token_type = NJS_TOKEN_DEFAULT;
                    dflt = branch;

                    type = njs_parser_token(vm, parser);
                    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                        return type;
                    }
                }

                *last = branch;
                last = &branch->left;

                type = njs_parser_match(vm, parser, type, NJS_TOKEN_COLON);
                if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                    return type;
                }

            } while (type == NJS_TOKEN_CASE || type == NJS_TOKEN_DEFAULT);

            parser->node = NULL;

            if (type == NJS_TOKEN_CLOSE_BRACE) {
                break;
            }

        } else if (branch == NULL) {
            /* The first switch statment is not "case/default" keyword. */
            return NJS_TOKEN_ILLEGAL;
        }

        type = njs_parser_statement_chain(vm, parser, type, 0);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        node->right = parser->node;
    }

    parser->node = swtch;

    return njs_parser_token(vm, parser);
}


static njs_token_type_t
njs_parser_while_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t   type;
    njs_parser_node_t  *node, *cond;

    type = njs_parser_grouping_expression(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    cond = parser->node;

    type = njs_parser_block(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_WHILE);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = parser->node;
    node->right = cond;
    parser->node = node;

    return type;
}


static njs_token_type_t
njs_parser_do_while_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t   type;
    njs_parser_node_t  *node, *stmt;

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_block(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    stmt = parser->node;

    if (njs_slow_path(type != NJS_TOKEN_WHILE)) {
        return NJS_TOKEN_ILLEGAL;
    }

    type = njs_parser_grouping_expression(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_DO);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = stmt;
    node->right = parser->node;
    parser->node = node;

    return type;
}


static njs_token_type_t
njs_parser_for_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_str_t          name;
    njs_token_type_t   type;
    njs_parser_node_t  *node, *init, *condition, *update, *cond, *body;

    init = NULL;
    condition = NULL;
    update = NULL;

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_OPEN_PARENTHESIS);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    if (type != NJS_TOKEN_SEMICOLON) {

        if (type == NJS_TOKEN_VAR) {

            type = njs_parser_var_statement(vm, parser, type, 1);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                return type;
            }

            init = parser->node;

            if (init->token_type == NJS_TOKEN_FOR_IN) {
                goto done;
            }

        } else {

            name = *njs_parser_text(parser);

            type = njs_parser_expression(vm, parser, type);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                return type;
            }

            init = parser->node;

            if (init->token_type == NJS_TOKEN_IN) {
                type = njs_parser_for_in_statement(vm, parser, &name, type);
                if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                    return type;
                }

                goto done;
            }
        }
    }

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_SEMICOLON);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    if (type != NJS_TOKEN_SEMICOLON) {

        type = njs_parser_expression(vm, parser, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        condition = parser->node;
    }

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_SEMICOLON);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    if (type != NJS_TOKEN_CLOSE_PARENTHESIS) {

        type = njs_parser_expression(vm, parser, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        update = parser->node;
    }

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_CLOSE_PARENTHESIS);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_block(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FOR);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    cond = njs_parser_node_new(vm, parser, 0);
    if (njs_slow_path(cond == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    body = njs_parser_node_new(vm, parser, 0);
    if (njs_slow_path(body == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = init;
    node->right = cond;

    cond->left = condition;
    cond->right = body;

    body->left = parser->node;
    body->right = update;

    parser->node = node;

done:

    return type;
}


static njs_token_type_t
njs_parser_var_in_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *name)
{
    njs_token_type_t   type;
    njs_parser_node_t  *node, *foreach;

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_expression(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_IN);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = name;
    node->right = parser->node;

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_CLOSE_PARENTHESIS);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_block(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    foreach = njs_parser_node_new(vm, parser, NJS_TOKEN_FOR_IN);
    if (njs_slow_path(foreach == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    foreach->left = node;
    foreach->right = parser->node;

    parser->node = foreach;

    return type;
}


static njs_token_type_t
njs_parser_for_in_statement(njs_vm_t *vm, njs_parser_t *parser, njs_str_t *name,
    njs_token_type_t type)
{
    njs_parser_node_t  *node;

    node = parser->node->left;

    if (node->token_type != NJS_TOKEN_NAME) {
        njs_parser_ref_error(vm, parser, "Invalid left-hand side \"%V\" "
                             "in for-in statement", name);

        return NJS_TOKEN_ILLEGAL;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FOR_IN);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->left = parser->node;

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_CLOSE_PARENTHESIS);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_block(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    node->right = parser->node;
    parser->node = node;

    return type;
}


static njs_token_type_t
njs_parser_brk_statement(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type)
{
    uintptr_t          unique_id;
    njs_int_t          ret;
    njs_str_t          name;
    njs_parser_node_t  *node;

    node = njs_parser_node_new(vm, parser, type);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token_line = njs_parser_token_line(parser);
    parser->node = node;

    type = njs_lexer_token(vm, parser->lexer);

    switch (type) {

    case NJS_TOKEN_LINE_END:
        return njs_parser_token(vm, parser);

    case NJS_TOKEN_NAME:
        name = *njs_parser_text(parser);
        unique_id = njs_parser_key_hash(parser);

        if (njs_label_find(vm, parser->scope, unique_id) == NULL) {
            njs_parser_syntax_error(vm, parser, "Undefined label \"%V\"",
                                    &name);
            return NJS_TOKEN_ILLEGAL;
        }

        ret = njs_name_copy(vm, &parser->node->name, &name);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_TOKEN_ERROR;
        }

        return njs_parser_token(vm, parser);

    case NJS_TOKEN_SEMICOLON:
    case NJS_TOKEN_CLOSE_BRACE:
    case NJS_TOKEN_END:
        return type;

    default:
        /* TODO: LABEL */
        return NJS_TOKEN_ILLEGAL;
    }
}


static njs_token_type_t
njs_parser_try_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_int_t          ret;
    njs_token_type_t   type;
    njs_parser_node_t  *node, *try, *catch;

    type = njs_parser_try_block(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    try = njs_parser_node_new(vm, parser, NJS_TOKEN_TRY);
    if (njs_slow_path(try == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    try->left = parser->node;

    if (type == NJS_TOKEN_CATCH) {
        type = njs_parser_token(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        type = njs_parser_match(vm, parser, type, NJS_TOKEN_OPEN_PARENTHESIS);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        if (type != NJS_TOKEN_NAME) {
            return NJS_TOKEN_ILLEGAL;
        }

        catch = njs_parser_node_new(vm, parser, NJS_TOKEN_CATCH);
        if (njs_slow_path(catch == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        try->right = catch;

        /*
         * The "catch" clause creates a block scope for single variable
         * which receives exception value.
         */
        ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_BLOCK);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_TOKEN_ERROR;
        }

        node = njs_parser_variable_node(vm, parser, njs_parser_key_hash(parser),
                                        NJS_VARIABLE_CATCH);
        if (njs_slow_path(node == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        catch->left = node;

        type = njs_parser_token(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        if (njs_slow_path(type != NJS_TOKEN_CLOSE_PARENTHESIS)) {
            return NJS_TOKEN_ILLEGAL;
        }

        type = njs_parser_try_block(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        catch->right = parser->node;

        njs_parser_scope_end(vm, parser);
    }

    if (type == NJS_TOKEN_FINALLY) {

        type = njs_parser_try_block(vm, parser);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        node = njs_parser_node_new(vm, parser, NJS_TOKEN_FINALLY);
        if (njs_slow_path(node == NULL)) {
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

    return type;
}


static njs_token_type_t
njs_parser_try_block(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t   type;
    njs_parser_node_t  *node;

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type != NJS_TOKEN_OPEN_BRACE)) {
        return NJS_TOKEN_ILLEGAL;
    }

    type = njs_parser_block_statement(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    node = parser->node;

    if (node != NULL && node->token_type == NJS_TOKEN_BLOCK) {
        parser->node = node->left;

        njs_mp_free(vm->mem_pool, node);
    }

    return type;
}


static njs_token_type_t
njs_parser_throw_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t   type;
    njs_parser_node_t  *node;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_THROW);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    type = njs_lexer_token(vm, parser->lexer);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    switch (type) {

    case NJS_TOKEN_LINE_END:
        njs_parser_syntax_error(vm, parser, "Illegal newline after throw");
        return NJS_TOKEN_ILLEGAL;

    default:
        type = njs_parser_expression(vm, parser, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        node->right = parser->node;
        parser->node = node;

        return type;
    }
}


static njs_token_type_t
njs_parser_import_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_int_t          ret;
    njs_token_type_t   type;
    njs_parser_node_t  *name, *import;

    if (parser->scope->type != NJS_SCOPE_GLOBAL
        && !parser->scope->module)
    {
        njs_parser_syntax_error(vm, parser, "Illegal import statement");

        return NJS_TOKEN_ERROR;
    }

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    if (type != NJS_TOKEN_NAME) {
        njs_parser_syntax_error(vm, parser,
                                "Non-default import is not supported");
        return NJS_TOKEN_ILLEGAL;
    }

    name = njs_parser_variable_node(vm, parser, njs_parser_key_hash(parser),
                                    NJS_VARIABLE_VAR);
    if (njs_slow_path(name == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_match_name(vm, parser, type, "from");
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    if (type != NJS_TOKEN_STRING) {
        return NJS_TOKEN_ILLEGAL;
    }

    ret = njs_parser_module(vm, parser);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_TOKEN_ERROR;
    }

    import = njs_parser_node_new(vm, parser, NJS_TOKEN_IMPORT);
    if (njs_slow_path(import == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    import->left = name;
    import->right = parser->node;

    parser->node = import;
    parser->node->hoist = 1;

    return njs_parser_token(vm, parser);
}


njs_token_type_t
njs_parser_module_lambda(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_int_t              ret;
    njs_token_type_t       type;
    njs_parser_node_t      *node, *parent;
    njs_function_lambda_t  *lambda;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FUNCTION_EXPRESSION);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token_line = njs_parser_token_line(parser);

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    lambda = njs_function_lambda_alloc(vm, 1);
    if (njs_slow_path(lambda == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->u.value.data.u.lambda = lambda;
    parser->node = node;

    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_FUNCTION);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_TOKEN_ERROR;
    }

    parser->scope->module = 1;

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_OPEN_PARENTHESIS);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    parent = parser->node;

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_CLOSE_PARENTHESIS);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_lambda_statements(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    ret = njs_parser_export_sink(vm, parser);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_TOKEN_ERROR;
    }

    parent->right = njs_parser_chain_top(parser);
    parent->right->token_line = 1;

    parser->node = parent;

    njs_parser_scope_end(vm, parser);

    return type;
}


static njs_token_type_t
njs_parser_export_statement(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t   type;
    njs_parser_node_t  *node;

    if (!parser->scope->module) {
        njs_parser_syntax_error(vm, parser, "Illegal export statement");
        return NJS_ERROR;
    }

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_EXPORT);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    parser->node = node;

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type != NJS_TOKEN_DEFAULT)) {
        njs_parser_syntax_error(vm, parser,
                                "Non-default export is not supported");
        return NJS_TOKEN_ILLEGAL;
    }

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_expression(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    node->right = parser->node;
    parser->node = node;

    return type;
}


static njs_int_t
njs_parser_export_sink(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_uint_t         n;
    njs_parser_node_t  *node, *prev;

    n = 0;

    for (node = njs_parser_chain_top(parser);
         node != NULL;
         node = node->left)
    {
        if (node->right != NULL
            && node->right->token_type == NJS_TOKEN_EXPORT)
        {
            n++;
        }
    }

    if (n != 1) {
        njs_parser_syntax_error(vm, parser,
             (n == 0) ? "export statement is required"
                      : "Identifier \"default\" has already been declared");
        return NJS_ERROR;
    }

    node = njs_parser_chain_top(parser);

    if (node->right && node->right->token_type == NJS_TOKEN_EXPORT) {
        return NJS_OK;
    }

    prev = njs_parser_chain_top(parser);

    while (prev->left != NULL) {
        node = prev->left;

        if (node->right != NULL
            && node->right->token_type == NJS_TOKEN_EXPORT)
        {
            prev->left = node->left;
            break;
        }

        prev = prev->left;
    }

    node->left = njs_parser_chain_top(parser);
    njs_parser_chain_top_set(parser, node);

    return NJS_OK;
}


static njs_token_type_t
njs_parser_grouping_expression(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t  type;

    type = njs_parser_token(vm, parser);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_OPEN_PARENTHESIS);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    type = njs_parser_expression(vm, parser, type);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    return njs_parser_match(vm, parser, type, NJS_TOKEN_CLOSE_PARENTHESIS);
}


njs_int_t
njs_parser_match_arrow_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type)
{
    size_t      offset;
    njs_bool_t  rest_parameters;

    if (type != NJS_TOKEN_OPEN_PARENTHESIS && type != NJS_TOKEN_NAME) {
        return NJS_DECLINED;
    }

    offset = 0;

    if (type == NJS_TOKEN_NAME) {
        goto arrow;
    }

    type = njs_parser_peek_token(vm, parser, &offset);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return NJS_DECLINED;
    }

    rest_parameters = 0;

    while (type != NJS_TOKEN_CLOSE_PARENTHESIS) {

        if (rest_parameters) {
            return NJS_DECLINED;
        }

        if (njs_slow_path(type == NJS_TOKEN_ELLIPSIS)) {
            rest_parameters = 1;

            type = njs_parser_peek_token(vm, parser, &offset);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
                return NJS_DECLINED;
            }
        }

        if (njs_slow_path(type != NJS_TOKEN_NAME)) {
            return NJS_DECLINED;
        }

        type = njs_parser_peek_token(vm, parser, &offset);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        if (type == NJS_TOKEN_COMMA) {
            type = njs_parser_peek_token(vm, parser, &offset);
            if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
               return NJS_DECLINED;
            }
        }
    }

arrow:

    type = njs_parser_peek_token(vm, parser, &offset);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return NJS_DECLINED;
    }

    if (parser->lexer->prev_type == NJS_TOKEN_LINE_END) {
        return NJS_DECLINED;
    }

    if (njs_slow_path(type != NJS_TOKEN_ARROW)) {
        return NJS_DECLINED;
    }

    return NJS_OK;
}


njs_token_type_t
njs_parser_arrow_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type)
{
    njs_int_t              ret;
    njs_index_t            index;
    njs_parser_node_t      *node, *body, *parent;
    njs_function_lambda_t  *lambda;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_FUNCTION_EXPRESSION);
    if (njs_slow_path(node == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->token_line = njs_parser_token_line(parser);
    parser->node = node;

    lambda = njs_function_lambda_alloc(vm, 0);
    if (njs_slow_path(lambda == NULL)) {
        return NJS_TOKEN_ERROR;
    }

    node->u.value.data.u.lambda = lambda;

    ret = njs_parser_scope_begin(vm, parser, NJS_SCOPE_FUNCTION);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_TOKEN_ERROR;
    }

    parser->scope->arrow_function = 1;

    index = NJS_SCOPE_ARGUMENTS;

    /* A "this" reservation. */
    index += sizeof(njs_value_t);

    if (type == NJS_TOKEN_OPEN_PARENTHESIS) {
        type = njs_parser_lambda_arguments(vm, parser, lambda, index, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

    } else {
        type = njs_parser_lambda_argument(vm, parser, index);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        lambda->nargs = 1;
    }

    if (parser->lexer->prev_type == NJS_TOKEN_LINE_END) {
        return NJS_TOKEN_ILLEGAL;
    }

    type = njs_parser_match(vm, parser, type, NJS_TOKEN_ARROW);
    if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
        return type;
    }

    if (type == NJS_TOKEN_OPEN_BRACE) {
        type = njs_parser_lambda_body(vm, parser, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

    } else {
        parent = parser->node;

        type = njs_parser_assignment_expression(vm, parser, type);
        if (njs_slow_path(type <= NJS_TOKEN_ILLEGAL)) {
            return type;
        }

        body = njs_parser_return_set(vm, parser, parser->node);
        if (njs_slow_path(body == NULL)) {
            return NJS_TOKEN_ERROR;
        }

        parent->right = body;
        parser->node = parent;
    }

    njs_parser_scope_end(vm, parser);

    return type;
}


njs_bool_t
njs_parser_has_side_effect(njs_parser_node_t *node)
{
    njs_bool_t  side_effect;

    if (node == NULL) {
        return 0;
    }

    if (node->token_type >= NJS_TOKEN_ASSIGNMENT
        && node->token_type <= NJS_TOKEN_LAST_ASSIGNMENT)
    {
        return 1;
    }

    if (node->token_type == NJS_TOKEN_FUNCTION_CALL
        || node->token_type == NJS_TOKEN_METHOD_CALL)
    {
        return 1;
    }

    side_effect = njs_parser_has_side_effect(node->left);

    if (njs_fast_path(!side_effect)) {
        return njs_parser_has_side_effect(node->right);
    }

    return side_effect;
}


njs_token_type_t
njs_parser_unexpected_token(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type)
{
    if (type != NJS_TOKEN_END) {
        njs_parser_syntax_error(vm, parser, "Unexpected token \"%V\"",
                                njs_parser_text(parser));

    } else {
        njs_parser_syntax_error(vm, parser, "Unexpected end of input");
    }

    return NJS_TOKEN_ILLEGAL;
}


u_char *
njs_parser_trace_handler(njs_trace_t *trace, njs_trace_data_t *td,
    u_char *start)
{
    u_char        *p;
    size_t        size;
    njs_vm_t      *vm;
    njs_lexer_t   *lexer;
    njs_parser_t  *parser;

    size = njs_length("InternalError: ");
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
    njs_object_type_t type, uint32_t line, const char *fmt, va_list args)
{
    size_t       width;
    u_char       msg[NJS_MAX_ERROR_STR];
    u_char       *p, *end;
    njs_str_t    *file;
    njs_int_t    ret;
    njs_value_t  value;

    static const njs_value_t  file_name = njs_string("fileName");
    static const njs_value_t  line_number = njs_string("lineNumber");

    file = &scope->file;

    p = msg;
    end = msg + NJS_MAX_ERROR_STR;

    p = njs_vsprintf(p, end, fmt, args);

    width = njs_length(" in ") + file->length + NJS_INT_T_LEN;

    if (p > end - width) {
        p = end - width;
    }

    if (file->length != 0 && !vm->options.quiet) {
        p = njs_sprintf(p, end, " in %V:%uD", file, line);

    } else {
        p = njs_sprintf(p, end, " in %uD", line);
    }

    njs_error_new(vm, &vm->retval, type, msg, p - msg);

    njs_set_number(&value, line);
    njs_value_property_set(vm, &vm->retval, njs_value_arg(&line_number),
                           &value);

    if (file->length != 0) {
        ret = njs_string_set(vm, &value, file->start, file->length);
        if (ret == NJS_OK) {
            njs_value_property_set(vm, &vm->retval, njs_value_arg(&file_name),
                                   &value);
        }
    }
}


void
njs_parser_lexer_error(njs_vm_t *vm, njs_parser_t *parser,
    njs_object_type_t type, const char *fmt, ...)
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
    njs_object_type_t type, const char *fmt, ...)
{
    va_list  args;

    va_start(args, fmt);
    njs_parser_scope_error(vm, node->scope, type, node->token_line, fmt, args);
    va_end(args);
}
