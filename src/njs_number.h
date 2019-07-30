
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_NUMBER_H_INCLUDED_
#define _NJS_NUMBER_H_INCLUDED_


#include <njs_string.h>
#include <math.h>


uint32_t njs_value_to_index(const njs_value_t *value);
double njs_number_dec_parse(const u_char **start, const u_char *end);
uint64_t njs_number_oct_parse(const u_char **start, const u_char *end);
uint64_t njs_number_bin_parse(const u_char **start, const u_char *end);
uint64_t njs_number_hex_parse(const u_char **start, const u_char *end);
int64_t njs_number_radix_parse(const u_char **start, const u_char *end,
    uint8_t radix);
njs_ret_t njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number);
njs_ret_t njs_number_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_ret_t njs_number_global_is_nan(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_ret_t njs_number_is_finite(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_ret_t njs_number_parse_int(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_ret_t njs_number_parse_float(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);


njs_inline int64_t
njs_number_to_int64(double num)
{
#if (NJS_NAN_TO_UINT_CONVERSION != 0)
    /*
     * PPC32: NaN and Inf are converted to 0x8000000080000000
     * and become non-zero after truncation.
     */

    if (isnan(num) || isinf(num)) {
        return 0;
    }
#endif

    /*
     * ES5.1: integer must be modulo 2^32.
     * 2^53 is the largest integer number which can be stored safely
     * in the IEEE-754 format and numbers less than 2^53 can be just
     * converted to int64_t eliding more expensive fmod() operation.
     * Then the int64 integer is truncated to uint32_t.  The NaN is
     * converted to 0x8000000000000000 and becomes 0 after truncation.
     * fmod() of the Infinity returns NaN.
     */

    if (fabs(num) > 9007199254740992.0) {
        return (int64_t) fmod(num, 4294967296.0);
    }

    return (int64_t) num;
}


njs_inline int32_t
njs_number_to_integer(double num)
{
    return (int32_t) njs_number_to_int64(num);
}


njs_inline int32_t
njs_number_to_int32(double num)
{
    return (int32_t) njs_number_to_int64(num);
}


njs_inline uint32_t
njs_number_to_uint32(double num)
{
    return (uint32_t) njs_number_to_int64(num);
}


njs_inline uint16_t
njs_number_to_uint16(double num)
{
    return (uint16_t) njs_number_to_int64(num);
}


njs_inline uint32_t
njs_number_to_length(double num)
{
#if (NJS_NAN_TO_UINT_CONVERSION != 0)
    if (isnan(num)) {
        return 0;
    }
#endif

    if (num > UINT32_MAX) {
        return UINT32_MAX;

    } else if (num < 0.0) {
        return 0;
    }

    return (uint32_t) (int64_t) num;
}


njs_inline njs_int_t
njs_char_to_hex(u_char c)
{
    c |= 0x20;

    /* Values less than '0' become >= 208. */
    c = c - '0';

    if (c > 9) {
        /* Values less than 'a' become >= 159. */
        c = c - ('a' - '0');

        if (njs_slow_path(c > 5)) {
            return -1;
        }

        c += 10;
    }

    return c;
}


njs_inline double
njs_primitive_value_to_number(const njs_value_t *value)
{
    if (njs_fast_path(njs_is_numeric(value))) {
        return njs_number(value);
    }

    return njs_string_to_number(value, 1);
}


njs_inline int32_t
njs_primitive_value_to_integer(const njs_value_t *value)
{
    return njs_number_to_integer(njs_primitive_value_to_number(value));
}


njs_inline int32_t
njs_primitive_value_to_int32(const njs_value_t *value)
{
    return njs_number_to_int32(njs_primitive_value_to_number(value));
}


njs_inline uint32_t
njs_primitive_value_to_uint32(const njs_value_t *value)
{
    return njs_number_to_uint32(njs_primitive_value_to_number(value));
}


njs_inline uint32_t
njs_primitive_value_to_length(const njs_value_t *value)
{
    return njs_number_to_length(njs_primitive_value_to_number(value));
}


njs_inline void
njs_uint32_to_string(njs_value_t *value, uint32_t u32)
{
    u_char  *dst, *p;

    dst = njs_string_short_start(value);
    p = njs_sprintf(dst, dst + NJS_STRING_SHORT, "%uD", u32);

    njs_string_short_set(value, p - dst, p - dst);
}


extern const njs_object_init_t  njs_number_constructor_init;
extern const njs_object_init_t  njs_number_prototype_init;

extern const njs_object_init_t  njs_is_nan_function_init;
extern const njs_object_init_t  njs_is_finite_function_init;
extern const njs_object_init_t  njs_parse_int_function_init;
extern const njs_object_init_t  njs_parse_float_function_init;


#endif /* _NJS_NUMBER_H_INCLUDED_ */
