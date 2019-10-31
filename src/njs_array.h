
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ARRAY_H_INCLUDED_
#define _NJS_ARRAY_H_INCLUDED_


#define NJS_ARRAY_MAX_INDEX      0xffffffff
#define NJS_ARRAY_INVALID_INDEX  NJS_ARRAY_MAX_INDEX

#define NJS_ARRAY_SPARE          8
#define NJS_ARRAY_MAX_LENGTH     (UINT32_MAX/ sizeof(njs_value_t))


njs_array_t *njs_array_alloc(njs_vm_t *vm, uint64_t length, uint32_t spare);
njs_int_t njs_array_add(njs_vm_t *vm, njs_array_t *array, njs_value_t *value);
njs_int_t njs_array_string_add(njs_vm_t *vm, njs_array_t *array,
    const u_char *start, size_t size, size_t length);
njs_int_t njs_array_expand(njs_vm_t *vm, njs_array_t *array, uint32_t prepend,
    uint32_t append);


extern const njs_object_init_t  njs_array_instance_init;
extern const njs_object_type_init_t  njs_array_type_init;


#endif /* _NJS_ARRAY_H_INCLUDED_ */
