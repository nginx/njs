
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_NUMBER_H_INCLUDED_
#define _NJS_NUMBER_H_INCLUDED_


#define NJS_MAX_LENGTH      (0x1fffffffffffffLL)
#define NJS_INT64_DBL_MIN   (-9.223372036854776e+18) /* closest to INT64_MIN */
#define NJS_INT64_DBL_MAX   (9.223372036854776e+18) /* closest to INT64_MAX */


double njs_key_to_index(const njs_value_t *value);
double njs_number_dec_parse(const u_char **start, const u_char *end,
    njs_bool_t literal);
double njs_number_oct_parse(const u_char **start, const u_char *end);
double njs_number_bin_parse(const u_char **start, const u_char *end);
double njs_number_hex_parse(const u_char **start, const u_char *end,
    njs_bool_t literal);
njs_int_t njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number);
njs_int_t njs_number_to_chain(njs_vm_t *vm, njs_chb_t *chain,
    double number);
njs_int_t njs_number_global_is_nan(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_int_t njs_number_global_is_finite(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_int_t njs_number_parse_int(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
njs_int_t njs_number_parse_float(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);


njs_inline njs_bool_t
njs_number_is_integer_index(double num)
{
    uint32_t  u32;

    u32 = num;

    return (u32 == num && u32 != 0xffffffff);
}


njs_inline njs_bool_t
njs_key_is_integer_index(double num, const njs_value_t *value)
{
    return (njs_number_is_integer_index(num))
            && !(njs_is_string(value) && num == 0 && signbit(num));
}


njs_inline int64_t
njs_number_to_integer(double num)
{
    if (njs_fast_path(!isnan(num))) {
        if (num < NJS_INT64_DBL_MIN) {
            return INT64_MIN;

        } else if (num > NJS_INT64_DBL_MAX) {
            return INT64_MAX;
        }

        return num;
    }

    return 0;
}


njs_inline int32_t
njs_number_to_int32(double num)
{
    uint32_t          r;
    uint64_t          v;
    njs_int_t         exp;
    njs_diyfp_conv_t  conv;

    conv.d = num;

    exp = (conv.u64 & NJS_DBL_EXPONENT_MASK) >> NJS_DBL_SIGNIFICAND_SIZE;

    if (njs_fast_path(exp < (NJS_DBL_EXPONENT_OFFSET + 31))) {
        /* |num| < 2**31. */
        return num;
    }

    if (exp < (NJS_DBL_EXPONENT_OFFSET + 31 + 53)) {
        v = (conv.u64 & NJS_DBL_SIGNIFICAND_MASK) | NJS_DBL_HIDDEN_BIT;
        v <<= (exp - NJS_DBL_EXPONENT_BIAS + 32);
        r = v >> 32;

        if (conv.u64 & NJS_DBL_SIGN_MASK) {
            r = -r;
        }

        return r;
    }

    /*
     * ES5.1: integer must be modulo 2^32.
     * The distance between larger doubles
     * (exp >= NJS_DBL_EXPONENT_OFFSET + 31 + 53) is a multiple of 2**32 => 0.
     * This also handles NaN and Inf.
     */

    return 0;
}


njs_inline uint32_t
njs_number_to_uint32(double num)
{
    return (uint32_t) njs_number_to_int32(num);
}


njs_inline uint16_t
njs_number_to_uint16(double num)
{
    return (uint16_t) njs_number_to_int32(num);
}


njs_inline uint64_t
njs_number_to_length(double num)
{
    if (isnan(num)) {
        return 0;
    }

    if (num > NJS_MAX_LENGTH) {
        return NJS_MAX_LENGTH;

    } else if (num < 0.0) {
        return 0;
    }

    return (uint64_t) num;
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


njs_inline void
njs_uint32_to_string(njs_value_t *value, uint32_t u32)
{
    u_char  *dst, *p;

    dst = njs_string_short_start(value);
    p = njs_sprintf(dst, dst + NJS_STRING_SHORT, "%uD", u32);

    njs_string_short_set(value, p - dst, p - dst);
}


extern const njs_object_type_init_t  njs_number_type_init;


#endif /* _NJS_NUMBER_H_INCLUDED_ */
