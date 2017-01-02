
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VARIABLE_H_INCLUDED_
#define _NJS_VARIABLE_H_INCLUDED_


typedef enum {
    NJS_VARIABLE_CONST = 0,
    NJS_VARIABLE_LET,
    NJS_VARIABLE_TYPEOF,
    NJS_VARIABLE_SHIM,
    NJS_VARIABLE_VAR,
    NJS_VARIABLE_FUNCTION,
} njs_variable_type_t;


typedef enum {
    NJS_NAME_REFERENCE = 0,
    NJS_NAME_DECLARATION,
    NJS_NAME_TYPEOF,
} njs_name_reference_t;


typedef struct {
    nxt_str_t             name;
    njs_variable_type_t   type:8;    /* 3 bits */

    njs_index_t           index;
    njs_value_t           value;
} njs_variable_t;


#define njs_global_variable_value(vm, var)                                    \
    (njs_value_t *) ((u_char *) vm->global_scope                              \
                      + njs_offset((var)->index) - NJS_INDEX_GLOBAL_OFFSET)


njs_variable_t *njs_builtin_add(njs_vm_t *vm, njs_parser_t *parser);
njs_variable_t *njs_variable_add(njs_vm_t *vm, njs_parser_t *parser,
    njs_variable_type_t type);
njs_ret_t njs_name_copy(njs_vm_t *vm, nxt_str_t *dst, nxt_str_t *src);

nxt_str_t *njs_vm_export_functions(njs_vm_t *vm);


#endif /* _NJS_VARIABLE_H_INCLUDED_ */
