
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_TIMEOUT_H_INCLUDED_
#define _NJS_TIMEOUT_H_INCLUDED_


njs_ret_t njs_set_timeout(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_clear_timeout(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);


extern const njs_object_init_t  njs_set_timeout_function_init;
extern const njs_object_init_t  njs_clear_timeout_function_init;

#endif /* _NJS_TIMEOUT_H_INCLUDED_ */
