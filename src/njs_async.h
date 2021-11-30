
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) Nginx, Inc.
 */

#ifndef _NJS_ASYNC_H_INCLUDED_
#define _NJS_ASYNC_H_INCLUDED_


typedef struct {
    njs_promise_capability_t  *capability;
    njs_frame_t               *await;
    uintptr_t                 index;
    u_char                    *pc;
} njs_async_ctx_t;


njs_int_t njs_async_function_frame_invoke(njs_vm_t *vm, njs_value_t *retval);
njs_int_t njs_await_fulfilled(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused);
njs_int_t njs_await_rejected(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused);


extern const njs_object_type_init_t  njs_async_function_type_init;
extern const njs_object_init_t  njs_async_function_instance_init;


#endif /* _NJS_ASYNC_H_INCLUDED_ */
