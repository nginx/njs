
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_BOOLEAN_H_INCLUDED_
#define _NJS_BOOLEAN_H_INCLUDED_


njs_ret_t njs_boolean_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

extern const njs_object_init_t  njs_boolean_constructor_init;
extern const njs_object_init_t  njs_boolean_prototype_init;


#endif /* _NJS_BOOLEAN_H_INCLUDED_ */
