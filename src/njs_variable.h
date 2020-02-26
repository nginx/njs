
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
    NJS_VARIABLE_SHIM,
    NJS_VARIABLE_VAR,
    NJS_VARIABLE_FUNCTION,
} njs_variable_type_t;


typedef struct {
    uintptr_t             unique_id;

    njs_variable_type_t   type:8;    /* 3 bits */
    uint8_t               argument;
    uint8_t               this_object;
    uint8_t               arguments_object;

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
    njs_uint_t            scope_index;  /* NJS_SCOPE_INDEX_... */
    njs_bool_t            not_defined;
} njs_variable_reference_t;


typedef struct {
    NJS_RBTREE_NODE       (node);
    uintptr_t             key;
    njs_variable_t        *variable;
} njs_variable_node_t;


njs_variable_t *njs_variable_add(njs_vm_t *vm, njs_parser_scope_t *scope,
    uintptr_t unique_id, njs_variable_type_t type);
njs_int_t njs_variables_copy(njs_vm_t *vm, njs_rbtree_t *variables,
    njs_rbtree_t *prev_variables);
njs_variable_t * njs_label_add(njs_vm_t *vm, njs_parser_scope_t *scope,
    uintptr_t unique_id);
njs_variable_t *njs_label_find(njs_vm_t *vm, njs_parser_scope_t *scope,
    uintptr_t unique_id);
njs_int_t njs_label_remove(njs_vm_t *vm, njs_parser_scope_t *scope,
    uintptr_t unique_id);
njs_int_t njs_variable_reference(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_parser_node_t *node, uintptr_t unique_id, njs_reference_type_t type);
njs_int_t njs_variables_scope_reference(njs_vm_t *vm,
    njs_parser_scope_t *scope);
njs_index_t njs_scope_next_index(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_uint_t scope_index, const njs_value_t *default_value);
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


njs_inline void
njs_variable_node_free(njs_vm_t *vm, njs_variable_node_t *node)
{
    njs_mp_free(vm->mem_pool, node);
}


#endif /* _NJS_VARIABLE_H_INCLUDED_ */
