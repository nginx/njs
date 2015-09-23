
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ARRAY_H_INCLUDED_
#define _NJS_ARRAY_H_INCLUDED_


#define NJS_ARRAY_SPARE  8

struct njs_array_s {
    /* Must be aligned to njs_value_t. */
    njs_object_t         object;
    uint32_t             size;
    uint32_t             length;
    njs_value_t          *start;
    njs_value_t          *data;
};


njs_array_t *njs_array_alloc(njs_vm_t *vm, uint32_t length, uint32_t spare);
njs_ret_t njs_array_realloc(njs_vm_t *vm, njs_array_t *array, uint32_t prepend,
    uint32_t size);
njs_ret_t njs_array_function(njs_vm_t *vm, njs_param_t *param);
nxt_int_t njs_array_function_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);
nxt_int_t njs_array_prototype_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);


#endif /* _NJS_ARRAY_H_INCLUDED_ */
