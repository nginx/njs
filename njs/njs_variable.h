
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
    nxt_str_t             name;

    njs_variable_type_t   type:8;    /* 3 bits */
    uint8_t               closure;   /* 1 bit  */
    uint8_t               argument;

    njs_index_t           index;
    njs_value_t           value;
} njs_variable_t;


#define njs_global_variable_value(vm, var)                                    \
    (njs_value_t *) ((u_char *) vm->global_scope                              \
         + njs_scope_offset((var)->index) - NJS_INDEX_GLOBAL_OFFSET)


typedef enum {
    NJS_DECLARATION = 0,
    NJS_REFERENCE,
    NJS_TYPEOF,
} njs_reference_type_t;


typedef struct {
    njs_reference_type_t  type;
    uint32_t              hash;
    nxt_str_t             name;
    njs_variable_t        *variable;
    njs_parser_scope_t    *scope;
    nxt_uint_t            scope_index;  /* NJS_SCOPE_INDEX_... */
} njs_variable_reference_t;


njs_variable_t *njs_variable_add(njs_vm_t *vm, njs_parser_scope_t *scope,
    nxt_str_t *name, uint32_t hash, njs_variable_type_t type);
njs_ret_t njs_variable_reference(njs_vm_t *vm, njs_parser_scope_t *scope,
    njs_parser_node_t *node, nxt_str_t *name, uint32_t hash,
    njs_reference_type_t type);
njs_ret_t njs_variables_scope_reference(njs_vm_t *vm,
    njs_parser_scope_t *scope);
njs_index_t njs_scope_next_index(njs_vm_t *vm, njs_parser_scope_t *scope,
    nxt_uint_t scope_index, const njs_value_t *default_value);
njs_ret_t njs_name_copy(njs_vm_t *vm, nxt_str_t *dst, nxt_str_t *src);

extern const nxt_lvlhsh_proto_t  njs_variables_hash_proto;


#endif /* _NJS_VARIABLE_H_INCLUDED_ */
