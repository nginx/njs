
/*
 * Copyright (C) Dmitry Volynsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_MODULE_H_INCLUDED_
#define _NJS_MODULE_H_INCLUDED_


typedef struct {
    njs_str_t                   name;
    njs_value_t                 value;
    njs_index_t                 index;
    njs_function_t              function;
} njs_mod_t;


njs_int_t njs_module_load(njs_vm_t *vm);
void njs_module_reset(njs_vm_t *vm);
njs_mod_t *njs_module_add(njs_vm_t *vm, njs_str_t *name, njs_bool_t shared);
njs_int_t njs_parser_module(njs_parser_t *parser, njs_lexer_token_t *token,
    njs_queue_link_t *current);
njs_int_t njs_module_require(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);


extern njs_module_t              *njs_modules[];
extern const njs_lvlhsh_proto_t  njs_modules_hash_proto;


#endif /* _NJS_MODULE_H_INCLUDED_ */
