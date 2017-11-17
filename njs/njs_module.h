
/*
 * Copyright (C) Dmitry Volynsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_MODULE_H_INCLUDED_
#define _NJS_MODULE_H_INCLUDED_

typedef struct {
    nxt_str_t                   name;
    njs_object_t                object;
} njs_module_t;


njs_ret_t njs_module_require(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

extern const nxt_lvlhsh_proto_t  njs_modules_hash_proto;


#endif /* _NJS_MODULE_H_INCLUDED_ */
