
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
njs_ret_t njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number);
njs_ret_t njs_number_function(njs_vm_t *vm, njs_param_t *param);
nxt_int_t njs_number_function_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);
nxt_int_t njs_number_prototype_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash);


#endif /* _NJS_NUMBER_H_INCLUDED_ */
