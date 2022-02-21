
/*
 * Copyright (C) Dmitry Volynsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_MODULE_H_INCLUDED_
#define _NJS_MODULE_H_INCLUDED_


struct njs_mod_s {
    njs_str_t                   name;
    njs_value_t                 value;
    njs_index_t                 index;
    njs_function_t              function;
};


njs_mod_t *njs_module_add(njs_vm_t *vm, njs_str_t *name);
njs_mod_t *njs_module_find(njs_vm_t *vm, njs_str_t *name,
    njs_bool_t shared);
njs_mod_t *njs_parser_module(njs_parser_t *parser, njs_str_t *name);
njs_int_t njs_module_require(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);


extern njs_module_t              *njs_modules[];
extern const njs_lvlhsh_proto_t  njs_modules_hash_proto;


#endif /* _NJS_MODULE_H_INCLUDED_ */
