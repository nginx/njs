
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VARIABLE_H_INCLUDED_
#define _NJS_VARIABLE_H_INCLUDED_


typedef enum {
    NJS_VARIABLE_CONST = 0,
    NJS_VARIABLE_LET,
    NJS_VARIABLE_CATCH,
    NJS_VARIABLE_VAR,
    NJS_VARIABLE_FUNCTION,
} njs_variable_type_t;


typedef struct {
    uintptr_t             unique_id;

    njs_variable_type_t   type:8;    /* 3 bits */
    njs_bool_t            argument;
    njs_bool_t            arguments_object;
    njs_bool_t            self;
    njs_bool_t            init;
    njs_bool_t            closure;
    njs_bool_t            function;

    njs_parser_scope_t    *scope;
    njs_parser_scope_t    *original;

    njs_index_t           index;
    njs_value_t           value;
} njs_variable_t;


typedef enum {
    NJS_DECLARATION = 0,
    NJS_REFERENCE,
    NJS_TYPEOF,
} njs_reference_type_t;


typedef struct {
    njs_reference_type_t  type;
    uintptr_t             unique_id;
    njs_variable_t        *variable;
    njs_parser_scope_t    *scope;
    njs_bool_t            not_defined;
} njs_variable_reference_t;


typedef struct {
    NJS_RBTREE_NODE       (node);
    uintptr_t             key;
    njs_variable_t        *variable;
} njs_variable_node_t;


njs_variable_t *njs_variable_add(njs_parser_t *parser,
    njs_parser_scope_t *scope, uintptr_t unique_id, njs_variable_type_t type);
njs_variable_t *njs_variable_function_add(njs_parser_t *parser,
    njs_parser_scope_t *scope, uintptr_t unique_id, njs_variable_type_t type);
njs_variable_t * njs_label_add(njs_vm_t *vm, njs_parser_scope_t *scope,
    uintptr_t unique_id);
njs_variable_t *njs_label_find(njs_vm_t *vm, njs_parser_scope_t *scope,
    uintptr_t unique_id);
njs_int_t njs_label_remove(njs_vm_t *vm, njs_parser_scope_t *scope,
    uintptr_t unique_id);
njs_variable_t *njs_variable_reference(njs_vm_t *vm, njs_parser_node_t *node);
njs_variable_t *njs_variable_scope_add(njs_parser_t *parser,
    njs_parser_scope_t *scope, njs_parser_scope_t *original,
    uintptr_t unique_id, njs_variable_type_t type, njs_index_t index);
njs_int_t njs_name_copy(njs_vm_t *vm, njs_str_t *dst, const njs_str_t *src);


njs_inline njs_variable_node_t *
njs_variable_node_alloc(njs_vm_t *vm, njs_variable_t *var, uintptr_t key)
{
    njs_variable_node_t  *node;

    node = njs_mp_zalloc(vm->mem_pool, sizeof(njs_variable_node_t));

    if (njs_fast_path(node != NULL)) {
        node->key = key;
        node->variable = var;
    }

    return node;
}

njs_inline njs_function_lambda_t *
njs_variable_lambda(njs_variable_t * var)
{
    if (njs_is_function(&var->value)) {
        /* may be set by generator in njs_generate_function_declaration(). */
        return njs_function(&var->value)->u.lambda;
    }

    return var->value.data.u.lambda;
}


njs_inline void
njs_variable_node_free(njs_vm_t *vm, njs_variable_node_t *node)
{
    njs_mp_free(vm->mem_pool, node);
}


#endif /* _NJS_VARIABLE_H_INCLUDED_ */
