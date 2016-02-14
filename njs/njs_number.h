
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_NUMBER_H_INCLUDED_
#define _NJS_NUMBER_H_INCLUDED_


#include <math.h>


#define NJS_NAN       NAN
#define NJS_INFINITY  INFINITY


#define njs_is_infinity(n)                                                    \
    isinf(n)


#define njs_is_nan(n)                                                         \
    isnan(n)


double njs_value_to_number(njs_value_t *value);
double njs_number_parse(const u_char **start, const u_char *end);
int64_t njs_hex_number_parse(u_char *p, u_char *end);
njs_ret_t njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number);
njs_ret_t njs_number_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);

extern const njs_object_init_t  njs_number_constructor_init;
extern const njs_object_init_t  njs_number_prototype_init;


#endif /* _NJS_NUMBER_H_INCLUDED_ */
