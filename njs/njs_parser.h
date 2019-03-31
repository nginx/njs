
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_PARSER_H_INCLUDED_
#define _NJS_PARSER_H_INCLUDED_


struct njs_parser_scope_s {
    njs_parser_node_t               *top;

    nxt_queue_link_t                link;
    nxt_queue_t                     nested;

    njs_parser_scope_t              *parent;
    nxt_lvlhsh_t                    labels;
    nxt_lvlhsh_t                    variables;
    nxt_lvlhsh_t                    references;

#define NJS_SCOPE_INDEX_LOCAL       0
#define NJS_SCOPE_INDEX_CLOSURE     1

    nxt_array_t                     *values[2];  /* Array of njs_value_t. */
    njs_index_t                     next_index[2];

    nxt_str_t                       cwd;
    nxt_str_t                       file;

    njs_scope_t                     type:8;
    uint8_t                         nesting;     /* 4 bits */
    uint8_t                         argument_closures;
    uint8_t                         arguments_object;
    uint8_t                         module;
};


struct njs_parser_node_s {
    njs_token_t                     token:16;
    uint8_t                         ctor:1;
    uint8_t                         temporary;    /* 1 bit  */
    uint32_t                        token_line;

    union {
        uint32_t                    length;
        njs_variable_reference_t    reference;
        njs_value_t                 value;
        njs_vmcode_operation_t      operation;
        njs_parser_node_t           *object;
    } u;

    nxt_str_t                       label;

    njs_index_t                     index;

    /*
     * The scope points to
     *   in global and function node: global or function scopes;
     *   in variable node: a scope where variable was referenced;
     *   in operation node: a scope to allocate indexes for temporary values.
     */
    njs_parser_scope_t              *scope;

    njs_parser_node_t               *left;
    njs_parser_node_t               *right;
    njs_parser_node_t               *dest;
};


struct njs_parser_s {
    njs_lexer_t                     *lexer;
    njs_parser_node_t               *node;
    njs_parser_scope_t              *scope;
};


nxt_int_t njs_parser(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_t *prev);
njs_token_t njs_parser_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
njs_token_t njs_parser_var_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
njs_token_t njs_parser_assignment_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_t token);
njs_token_t njs_parser_function_expression(njs_vm_t *vm, njs_parser_t *parser);
njs_token_t njs_parser_module_lambda(njs_vm_t *vm, njs_parser_t *parser);
njs_token_t njs_parser_terminal(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
njs_token_t njs_parser_property_token(njs_vm_t *vm, njs_parser_t *parser);
nxt_int_t njs_parser_string_create(njs_vm_t *vm, njs_value_t *value);
njs_token_t njs_parser_lambda_statements(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
njs_variable_t *njs_variable_resolve(njs_vm_t *vm, njs_parser_node_t *node);
njs_index_t njs_variable_typeof(njs_vm_t *vm, njs_parser_node_t *node);
njs_index_t njs_variable_index(njs_vm_t *vm, njs_parser_node_t *node);
nxt_bool_t njs_parser_has_side_effect(njs_parser_node_t *node);
njs_token_t njs_parser_unexpected_token(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_t token);
u_char *njs_parser_trace_handler(nxt_trace_t *trace, nxt_trace_data_t *td,
    u_char *start);
void njs_parser_lexer_error(njs_vm_t *vm, njs_parser_t *parser,
    njs_value_type_t type, const char *fmt, ...);
void njs_parser_node_error(njs_vm_t *vm, njs_parser_node_t *node,
    njs_value_type_t type, const char *fmt, ...);


#define njs_parser_is_lvalue(node)                                            \
    ((node)->token == NJS_TOKEN_NAME || (node)->token == NJS_TOKEN_PROPERTY)


#define njs_scope_accumulative(vm, scope)                                     \
    ((vm)->options.accumulative && (scope)->type == NJS_SCOPE_GLOBAL)


#define njs_parser_text(parser)                                               \
    &(parser)->lexer->lexer_token->text


#define njs_parser_key_hash(parser)                                           \
    (parser)->lexer->lexer_token->key_hash


#define njs_parser_number(parser)                                             \
    (parser)->lexer->lexer_token->number


#define njs_parser_token_line(parser)                                         \
    (parser)->lexer->lexer_token->token_line


#define njs_parser_syntax_error(vm, parser, fmt, ...)                         \
    njs_parser_lexer_error(vm, parser, NJS_OBJECT_SYNTAX_ERROR, fmt,          \
                           ##__VA_ARGS__)


#define njs_parser_ref_error(vm, parser, fmt, ...)                            \
    njs_parser_lexer_error(vm, parser, NJS_OBJECT_REF_ERROR, fmt, ##__VA_ARGS__)


nxt_inline njs_token_t
njs_parser_token(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_t  token;

    do {
        token = njs_lexer_token(vm, parser->lexer);
    } while (nxt_slow_path(token == NJS_TOKEN_LINE_END));

    return token;
}


nxt_inline njs_token_t
njs_parser_peek_token(njs_vm_t *vm, njs_parser_t *parser, size_t *offset)
{
    njs_token_t  token;

    do {
        token = njs_lexer_peek_token(vm, parser->lexer, (*offset)++);
    } while (nxt_slow_path(token == NJS_TOKEN_LINE_END));

    return token;
}


nxt_inline njs_parser_node_t *
njs_parser_node_new(njs_vm_t *vm, njs_parser_t *parser, njs_token_t token)
{
    njs_parser_node_t  *node;

    node = nxt_mp_zalloc(vm->mem_pool, sizeof(njs_parser_node_t));

    if (nxt_fast_path(node != NULL)) {
        node->token = token;
        node->scope = parser->scope;
    }

    return node;
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


nxt_inline njs_parser_scope_t *
njs_parser_global_scope(njs_vm_t *vm)
{
    njs_parser_scope_t  *scope;

    scope = vm->parser->scope;

    while (scope->type != NJS_SCOPE_GLOBAL) {
        scope = scope->parent;
    }

    return scope;
}


extern const nxt_lvlhsh_proto_t  njs_keyword_hash_proto;


#endif /* _NJS_PARSER_H_INCLUDED_ */
