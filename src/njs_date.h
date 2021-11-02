
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_DATE_H_INCLUDED_
#define _NJS_DATE_H_INCLUDED_


njs_date_t *njs_date_alloc(njs_vm_t *vm, double time);
njs_int_t njs_date_to_string(njs_vm_t *vm, njs_value_t *retval,
    const njs_value_t *date);


extern const njs_object_type_init_t  njs_date_type_init;


#endif /* _NJS_DATE_H_INCLUDED_ */
