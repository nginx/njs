
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_SYMBOL_H_INCLUDED_
#define _NJS_SYMBOL_H_INCLUDED_

njs_int_t njs_symbol_to_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *value, njs_bool_t as_name);


extern const njs_object_type_init_t  njs_symbol_type_init;


#endif /* _NJS_SYMBOL_H_INCLUDED_ */
