
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_TIMER_H_INCLUDED_
#define _NJS_TIMER_H_INCLUDED_


njs_int_t njs_set_timeout(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_int_t njs_set_immediate(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_int_t njs_clear_timeout(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);


#endif /* _NJS_TIMER_H_INCLUDED_ */
