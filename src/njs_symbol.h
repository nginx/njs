
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_SYMBOL_H_INCLUDED_
#define _NJS_SYMBOL_H_INCLUDED_

const njs_value_t *njs_symbol_description(const njs_value_t *value);
njs_int_t njs_symbol_descriptive_string(njs_vm_t *vm, njs_value_t *dst,
    const njs_value_t *value);


extern const njs_object_type_init_t  njs_symbol_type_init;


#endif /* _NJS_SYMBOL_H_INCLUDED_ */
