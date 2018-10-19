
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_NUMBER_H_INCLUDED_
#define _NJS_NUMBER_H_INCLUDED_


#include <math.h>
#include <stdio.h>


uint32_t njs_value_to_index(const njs_value_t *value);
double njs_primitive_value_to_number(const njs_value_t *value);
uint32_t njs_primitive_value_to_integer(const njs_value_t *value);
double njs_number_dec_parse(const u_char **start, const u_char *end);
uint64_t njs_number_oct_parse(const u_char **start, const u_char *end);
uint64_t njs_number_bin_parse(const u_char **start, const u_char *end);
uint64_t njs_number_hex_parse(const u_char **start, const u_char *end);
int64_t njs_number_radix_parse(const u_char **start, const u_char *end,
    uint8_t radix);
njs_ret_t njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number);
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


nxt_inline nxt_int_t
njs_char_to_hex(u_char c)
{
    c |= 0x20;

    /* Values less than '0' become >= 208. */
    c = c - '0';

    if (c > 9) {
        /* Values less than 'a' become >= 159. */
        c = c - ('a' - '0');

        if (nxt_slow_path(c > 5)) {
            return -1;
        }

        c += 10;
    }

    return c;
}


nxt_inline void
njs_uint32_to_string(njs_value_t *value, uint32_t u32)
{
    size_t  size;

    size = snprintf((char *) njs_string_short_start(value),
                    NJS_STRING_SHORT, "%u", u32);
    njs_string_short_set(value, size, size);
}


extern const njs_object_init_t  njs_number_constructor_init;
extern const njs_object_init_t  njs_number_prototype_init;

extern const njs_object_init_t  njs_is_nan_function_init;
extern const njs_object_init_t  njs_is_finite_function_init;
extern const njs_object_init_t  njs_parse_int_function_init;
extern const njs_object_init_t  njs_parse_float_function_init;


#endif /* _NJS_NUMBER_H_INCLUDED_ */
