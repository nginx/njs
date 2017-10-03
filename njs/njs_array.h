
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ARRAY_H_INCLUDED_
#define _NJS_ARRAY_H_INCLUDED_


#define NJS_ARRAY_MAX_LENGTH     0xffffffff
/* The maximum valid array index is the maximum array length minus 1. */
#define NJS_ARRAY_INVALID_INDEX  NJS_ARRAY_MAX_LENGTH

#define NJS_ARRAY_SPARE  8


njs_array_t *njs_array_alloc(njs_vm_t *vm, uint32_t length, uint32_t spare);
njs_ret_t njs_array_add(njs_vm_t *vm, njs_array_t *array, njs_value_t *value);
njs_ret_t njs_array_string_add(njs_vm_t *vm, njs_array_t *array, u_char *start,
    size_t size, size_t length);
njs_ret_t njs_array_expand(njs_vm_t *vm, njs_array_t *array, uint32_t prepend,
    uint32_t size);
njs_ret_t njs_array_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

extern const njs_object_init_t  njs_array_constructor_init;
extern const njs_object_init_t  njs_array_prototype_init;


#endif /* _NJS_ARRAY_H_INCLUDED_ */
