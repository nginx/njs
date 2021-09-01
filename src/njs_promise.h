
/*
 * Copyright (C) Nginx, Inc.
 */

#ifndef _NJS_PROMISE_H_INCLUDED_
#define _NJS_PROMISE_H_INCLUDED_


typedef enum {
    NJS_PROMISE_PENDING = 0,
    NJS_PROMISE_FULFILL,
    NJS_PROMISE_REJECTED
} njs_promise_type_t;

typedef struct {
    njs_value_t               promise;
    njs_value_t               resolve;
    njs_value_t               reject;
} njs_promise_capability_t;

typedef struct {
    njs_promise_type_t        state;
    njs_value_t               result;
    njs_queue_t               fulfill_queue;
    njs_queue_t               reject_queue;
    njs_bool_t                is_handled;
} njs_promise_data_t;


njs_int_t njs_promise_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_promise_capability_t *njs_promise_new_capability(njs_vm_t *vm,
    njs_value_t *constructor);
njs_function_t *njs_promise_create_function(njs_vm_t *vm, size_t context_size);
njs_int_t njs_promise_perform_then(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *fulfilled, njs_value_t *rejected,
    njs_promise_capability_t *capability);
njs_promise_t *njs_promise_resolve(njs_vm_t *vm, njs_value_t *constructor,
    njs_value_t *x);


extern const njs_object_type_init_t  njs_promise_type_init;


#endif /* _NJS_PROMISE_H_INCLUDED_ */
