
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_PARSER_H_INCLUDED_
#define _NJS_PARSER_H_INCLUDED_


struct njs_parser_scope_s {
    njs_parser_node_t               *top;

    njs_queue_link_t                link;
    njs_queue_t                     nested;

    njs_parser_scope_t              *parent;
    njs_rbtree_t                    variables;
    njs_rbtree_t                    labels;
    njs_rbtree_t                    references;

#define NJS_SCOPE_INDEX_LOCAL       0
#define NJS_SCOPE_INDEX_CLOSURE     1

    njs_arr_t                       *values[2];  /* Array of njs_value_t. */
    njs_index_t                     next_index[2];

    njs_str_t                       cwd;
    njs_str_t                       file;

    njs_scope_t                     type:8;
    uint8_t                         nesting;     /* 4 bits */
    uint8_t                         argument_closures;
    uint8_t                         module;
    uint8_t                         arrow_function;
};


struct njs_parser_node_s {
    njs_token_type_t                token_type:16;
    uint8_t                         ctor:1;
    uint8_t                         temporary;    /* 1 bit  */
    uint8_t                         hoist;        /* 1 bit  */
    uint32_t                        token_line;

    union {
        uint32_t                    length;
        njs_variable_reference_t    reference;
        njs_value_t                 value;
        njs_vmcode_operation_t      operation;
        njs_parser_node_t           *object;
    } u;

    njs_str_t                       name;

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
    njs_uint_t                      count;
};


typedef struct {
    NJS_RBTREE_NODE                 (node);
    uintptr_t                       key;
    njs_parser_node_t               *parser_node;
} njs_parser_rbtree_node_t;


intptr_t njs_parser_scope_rbtree_compare(njs_rbtree_node_t *node1,
    njs_rbtree_node_t *node2);
njs_int_t njs_parser(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_t *prev);
njs_token_type_t njs_parser_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type);
njs_token_type_t njs_parser_assignment_expression(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_type_t type);
njs_token_type_t njs_parser_function_expression(njs_vm_t *vm,
    njs_parser_t *parser);
njs_int_t njs_parser_match_arrow_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type);
njs_token_type_t njs_parser_arrow_expression(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type);
njs_token_type_t njs_parser_module_lambda(njs_vm_t *vm, njs_parser_t *parser);
njs_token_type_t njs_parser_terminal(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type);
njs_token_type_t njs_parser_template_literal(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *parent);
njs_parser_node_t *njs_parser_argument(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *expr, njs_index_t index);
njs_int_t njs_parser_string_create(njs_vm_t *vm, njs_value_t *value);
njs_token_type_t njs_parser_function_lambda(njs_vm_t *vm, njs_parser_t *parser,
    njs_function_lambda_t *lambda, njs_token_type_t type);
njs_token_type_t njs_parser_lambda_statements(njs_vm_t *vm,
    njs_parser_t *parser, njs_token_type_t type);
njs_variable_t *njs_variable_resolve(njs_vm_t *vm, njs_parser_node_t *node);
njs_index_t njs_variable_typeof(njs_vm_t *vm, njs_parser_node_t *node);
njs_index_t njs_variable_index(njs_vm_t *vm, njs_parser_node_t *node);
njs_bool_t njs_parser_has_side_effect(njs_parser_node_t *node);
njs_token_type_t njs_parser_unexpected_token(njs_vm_t *vm, njs_parser_t *parser,
    njs_token_type_t type);
u_char *njs_parser_trace_handler(njs_trace_t *trace, njs_trace_data_t *td,
    u_char *start);
void njs_parser_lexer_error(njs_vm_t *vm, njs_parser_t *parser,
    njs_object_type_t type, const char *fmt, ...);
void njs_parser_node_error(njs_vm_t *vm, njs_parser_node_t *node,
    njs_object_type_t type, const char *fmt, ...);


#define njs_parser_enter(vm, parser)                                          \
    do {                                                                      \
        if (njs_slow_path((parser)->count++ > 4096)) {                        \
            njs_range_error(vm, "Maximum call stack size exceeded");          \
            return NJS_TOKEN_ERROR;                                           \
        }                                                                     \
    } while (0)


#define njs_parser_leave(parser) ((parser)->count--)


#define njs_parser_restricted_identifier(token)                               \
    (token == NJS_TOKEN_ARGUMENTS || token == NJS_TOKEN_EVAL)


#define njs_parser_is_lvalue(node)                                            \
    ((node)->token_type == NJS_TOKEN_NAME                                     \
     || (node)->token_type == NJS_TOKEN_PROPERTY)


#define njs_scope_accumulative(vm, scope)                                     \
    ((vm)->options.accumulative && (scope)->type == NJS_SCOPE_GLOBAL)


#define njs_parser_text(parser)                                               \
    &(parser)->lexer->token->text


#define njs_parser_key_hash(parser)                                           \
    (parser)->lexer->token->unique_id


#define njs_parser_number(parser)                                             \
    (parser)->lexer->token->number


#define njs_parser_token_line(parser)                                         \
    (parser)->lexer->token->line


#define njs_parser_syntax_error(vm, parser, fmt, ...)                         \
    njs_parser_lexer_error(vm, parser, NJS_OBJ_TYPE_SYNTAX_ERROR, fmt,        \
                           ##__VA_ARGS__)


#define njs_parser_ref_error(vm, parser, fmt, ...)                            \
    njs_parser_lexer_error(vm, parser, NJS_OBJ_TYPE_REF_ERROR, fmt,           \
                           ##__VA_ARGS__)


njs_inline njs_token_type_t
njs_parser_token(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_token_type_t  type;

    do {
        type = njs_lexer_token(vm, parser->lexer);
    } while (njs_slow_path(type == NJS_TOKEN_LINE_END));

    return type;
}


njs_inline njs_token_type_t
njs_parser_peek_token(njs_vm_t *vm, njs_parser_t *parser, size_t *offset)
{
    njs_token_type_t  type;

    do {
        type = njs_lexer_peek_token(vm, parser->lexer, (*offset)++);
    } while (njs_slow_path(type == NJS_TOKEN_LINE_END));

    return type;
}


njs_inline njs_parser_node_t *
njs_parser_node_new(njs_vm_t *vm, njs_parser_t *parser, njs_token_type_t type)
{
    njs_parser_node_t  *node;

    node = njs_mp_zalloc(vm->mem_pool, sizeof(njs_parser_node_t));

    if (njs_fast_path(node != NULL)) {
        node->token_type = type;
        node->scope = parser->scope;
    }

    return node;
}


njs_inline njs_parser_node_t *
njs_parser_node_string(njs_vm_t *vm, njs_parser_t *parser)
{
    njs_int_t          ret;
    njs_parser_node_t  *node;

    node = njs_parser_node_new(vm, parser, NJS_TOKEN_STRING);
    if (njs_slow_path(node == NULL)) {
        return NULL;
    }

    ret = njs_parser_string_create(vm, &node->u.value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return node;
}


njs_inline njs_token_type_t
njs_parser_match(njs_vm_t *vm, njs_parser_t *parser, njs_token_type_t type,
    njs_token_type_t match)
{
    if (njs_fast_path(type == match)) {
        return njs_parser_token(vm, parser);
    }

    return njs_parser_unexpected_token(vm, parser, type);
}


njs_inline njs_token_type_t
njs_parser_match_name(njs_vm_t *vm, njs_parser_t *parser, njs_token_type_t type,
    const char *name)
{
    size_t     len;
    njs_str_t  *text;

    len = njs_strlen(name);
    text = njs_parser_text(parser);

    if (njs_fast_path(type == NJS_TOKEN_NAME
                      && text->length == len
                      && memcmp(text->start, name, len) == 0))
    {
        return njs_parser_token(vm, parser);
    }

    return njs_parser_unexpected_token(vm, parser, type);
}


njs_inline njs_variable_t *
njs_parser_variable_add(njs_vm_t *vm, njs_parser_t *parser,
    njs_variable_type_t type)
{
    return njs_variable_add(vm, parser->scope,
                            njs_parser_key_hash(parser), type);
}


njs_inline njs_int_t
njs_parser_variable_reference(njs_vm_t *vm, njs_parser_t *parser,
    njs_parser_node_t *node, njs_reference_type_t type)
{
    return njs_variable_reference(vm, parser->scope, node,
                                  njs_parser_key_hash(parser), type);
}


njs_inline njs_parser_scope_t *
njs_parser_global_scope(njs_vm_t *vm)
{
    njs_parser_scope_t  *scope;

    scope = vm->parser->scope;

    while (scope->type != NJS_SCOPE_GLOBAL) {
        scope = scope->parent;
    }

    return scope;
}


njs_inline njs_parser_scope_t *
njs_function_scope(njs_parser_scope_t *scope, njs_bool_t any)
{
    while (scope->type != NJS_SCOPE_GLOBAL) {
        if (scope->type == NJS_SCOPE_FUNCTION
            && (any || !scope->arrow_function))
        {
            return scope;
        }

        scope = scope->parent;
    }

    return NULL;
}


#endif /* _NJS_PARSER_H_INCLUDED_ */
