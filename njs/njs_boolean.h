
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_BOOLEAN_H_INCLUDED_
#define _NJS_BOOLEAN_H_INCLUDED_


njs_ret_t njs_boolean_function(njs_vm_t *vm, njs_param_t *param);
nxt_int_t njs_boolean_function_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);
nxt_int_t njs_boolean_prototype_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);


#endif /* _NJS_BOOLEAN_H_INCLUDED_ */
