
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
    njs_str_t             name;

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
    uint32_t              hash;
    njs_str_t             name;
    njs_variable_t        *variable;
    njs_parser_scope_t    *scope;
    njs_uint_t            scope_index;  /* NJS_SCOPE_INDEX_... */
    njs_bool_t            not_defined;
} njs_variable_reference_t;


njs_variable_t *njs_variable_add(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_str_t *name, uint32_t hash, njs_variable_type_t type);
njs_int_t njs_variables_copy(njs_vm_t *vm, njs_lvlhsh_t *variables,
    njs_lvlhsh_t *prev_variables);
njs_variable_t * njs_label_add(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_str_t *name, uint32_t hash);
njs_variable_t *njs_label_find(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_str_t *name, uint32_t hash);
njs_int_t njs_label_remove(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_str_t *name, uint32_t hash);
njs_int_t njs_variable_reference(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_parser_node_t *node, njs_str_t *name, uint32_t hash,
    njs_reference_type_t type);
njs_int_t njs_variables_scope_reference(njs_vm_t *vm,
    njs_parser_scope_t *scope);
njs_index_t njs_scope_next_index(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_uint_t scope_index, const njs_value_t *default_value);
njs_int_t njs_name_copy(njs_vm_t *vm, njs_str_t *dst, njs_str_t *src);

extern const njs_lvlhsh_proto_t  njs_variables_hash_proto;


#endif /* _NJS_VARIABLE_H_INCLUDED_ */
