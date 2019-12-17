
/*
 * Copyright (C) Nginx, Inc.
 */

#ifndef _NJS_PROMISE_H_INCLUDED_
#define _NJS_PROMISE_H_INCLUDED_


njs_int_t
njs_promise_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused);


extern const njs_object_type_init_t  njs_promise_type_init;


#endif /* _NJS_PROMISE_H_INCLUDED_ */
