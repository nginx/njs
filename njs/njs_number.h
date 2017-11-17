
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_NUMBER_H_INCLUDED_
#define _NJS_NUMBER_H_INCLUDED_


#include <math.h>


uint32_t njs_value_to_index(njs_value_t *value);
double njs_number_dec_parse(u_char **start, u_char *end);
uint64_t njs_number_oct_parse(u_char **start, u_char *end);
uint64_t njs_number_hex_parse(u_char **start, u_char *end);
int64_t njs_number_radix_parse(u_char **start, u_char *end, uint8_t radix);
njs_ret_t njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number);
size_t njs_num_to_buf(double num, u_char *buf, size_t size);
njs_ret_t njs_number_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_number_global_is_nan(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_number_is_finite(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_number_parse_int(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
njs_ret_t njs_number_parse_float(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused);
nxt_noinline uint32_t njs_number_to_integer(double num);


extern const njs_object_init_t  njs_number_constructor_init;
extern const njs_object_init_t  njs_number_prototype_init;


#endif /* _NJS_NUMBER_H_INCLUDED_ */
