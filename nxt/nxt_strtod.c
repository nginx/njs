/*
 * An internal strtod() implementation based upon V8 src/strtod.cc
 * without bignum support.
 *
 * Copyright 2012 the V8 project authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_diyfp.h>
#include <nxt_strtod.h>

/*
 * Max double: 1.7976931348623157 x 10^308
 * Min non-zero double: 4.9406564584124654 x 10^-324
 * Any x >= 10^309 is interpreted as +infinity.
 * Any x <= 10^-324 is interpreted as 0.
 * Note that 2.5e-324 (despite being smaller than the min double)
 * will be read as non-zero (equal to the min non-zero double).
 */

#define NXT_DECIMAL_POWER_MAX           309
#define NXT_DECIMAL_POWER_MIN           (-324)

#define NXT_UINT64_MAX                  nxt_uint64(0xFFFFFFFF, 0xFFFFFFFF)
#define NXT_UINT64_DECIMAL_DIGITS_MAX   19


/*
 * Reads digits from the buffer and converts them to a uint64.
 * Reads in as many digits as fit into a uint64.
 * When the string starts with "1844674407370955161" no further digit is read.
 * Since 2^64 = 18446744073709551616 it would still be possible read another
 * digit if it was less or equal than 6, but this would complicate the code.
 */
nxt_inline uint64_t
nxt_read_uint64(const u_char *start, size_t length, size_t *ndigits)
{
    u_char        d;
    uint64_t      value;
    const u_char  *p, *e;

    value = 0;

    p = start;
    e = p + length;

    while (p < e && value <= (NXT_UINT64_MAX / 10 - 1)) {
        d = *p++ - '0';
        value = 10 * value + d;
    }

    *ndigits = p - start;

    return value;
}


/*
 * Reads a nxt_diyfp_t from the buffer.
 * The returned nxt_diyfp_t is not necessarily normalized.
 * If remaining is zero then the returned nxt_diyfp_t is accurate.
 * Otherwise it has been rounded and has error of at most 1/2 ulp.
 */
static nxt_diyfp_t
nxt_diyfp_read(const u_char *start, size_t length, int *remaining)
{
    size_t    read;
    uint64_t  significand;

    significand = nxt_read_uint64(start, length, &read);

    /* Round the significand. */

    if (length != read) {
        if (start[read] >= '5') {
            significand++;
        }
    }

    *remaining = length - read;

    return nxt_diyfp(significand, 0);
}


/*
 * Returns 10^exp as an exact nxt_diyfp_t.
 * The given exp must be in the range [1; NXT_DECIMAL_EXPONENT_DIST[.
 */
nxt_inline nxt_diyfp_t
nxt_adjust_pow10(int exp)
{
    switch (exp) {
    case 1:
        return nxt_diyfp(nxt_uint64(0xa0000000, 00000000), -60);
    case 2:
        return nxt_diyfp(nxt_uint64(0xc8000000, 00000000), -57);
    case 3:
        return nxt_diyfp(nxt_uint64(0xfa000000, 00000000), -54);
    case 4:
        return nxt_diyfp(nxt_uint64(0x9c400000, 00000000), -50);
    case 5:
        return nxt_diyfp(nxt_uint64(0xc3500000, 00000000), -47);
    case 6:
        return nxt_diyfp(nxt_uint64(0xf4240000, 00000000), -44);
    case 7:
        return nxt_diyfp(nxt_uint64(0x98968000, 00000000), -40);
    default:
        nxt_unreachable();
        return nxt_diyfp(0, 0);
    }
}


/*
 * Returns the significand size for a given order of magnitude.
 * If v = f*2^e with 2^p-1 <= f <= 2^p then p+e is v's order of magnitude.
 * This function returns the number of significant binary digits v will have
 * once its encoded into a double. In almost all cases this is equal to
 * NXT_SIGNIFICAND_SIZE. The only exception are denormals. They start with
 * leading zeroes and their effective significand-size is hence smaller.
 */
nxt_inline int
nxt_diyfp_sgnd_size(int order)
{
    if (order >= (NXT_DBL_EXPONENT_DENORMAL + NXT_SIGNIFICAND_SIZE)) {
        return NXT_SIGNIFICAND_SIZE;
    }

    if (order <= NXT_DBL_EXPONENT_DENORMAL) {
        return 0;
    }

    return order - NXT_DBL_EXPONENT_DENORMAL;
}


#define NXT_DENOM_LOG   3
#define NXT_DENOM       (1 << NXT_DENOM_LOG)

/*
 * Returns either the correct double or the double that is just below
 * the correct double.
 */
static double
nxt_diyfp_strtod(const u_char *start, size_t length, int exp)
{
    int          magnitude, prec_digits;
    int          remaining, dec_exp, adj_exp, orig_e, shift;
    int64_t      error;
    uint64_t     prec_bits, half_way;
    nxt_diyfp_t  value, pow, adj_pow, rounded;

    value = nxt_diyfp_read(start, length, &remaining);

    exp += remaining;

    /*
     * Since some digits may have been dropped the value is not accurate.
     * If remaining is different than 0 than the error is at most .5 ulp
     * (unit in the last place).
     * Using a common denominator to avoid dealing with fractions.
     */

    error = (remaining == 0 ? 0 : NXT_DENOM / 2);

    orig_e = value.exp;
    value = nxt_diyfp_normalize(value);
    error <<= orig_e - value.exp;

    if (exp < NXT_DECIMAL_EXPONENT_MIN) {
        return 0.0;
    }

    pow = nxt_cached_power_dec(exp, &dec_exp);

    if (dec_exp != exp) {

        adj_exp = exp - dec_exp;
        adj_pow = nxt_adjust_pow10(exp - dec_exp);
        value = nxt_diyfp_mul(value, adj_pow);

        if (NXT_UINT64_DECIMAL_DIGITS_MAX - (int) length < adj_exp) {
            /*
             * The adjustment power is exact. There is hence only
             * an error of 0.5.
             */
            error += NXT_DENOM / 2;
        }
    }

    value = nxt_diyfp_mul(value, pow);

    /*
     * The error introduced by a multiplication of a * b equals
     *  error_a + error_b + error_a * error_b / 2^64 + 0.5
     *  Substituting a with 'value' and b with 'pow':
     *  error_b = 0.5  (all cached powers have an error of less than 0.5 ulp),
     *  error_ab = 0 or 1 / NXT_DENOM > error_a * error_b / 2^64.
     */

    error += NXT_DENOM + (error != 0 ? 1 : 0);

    orig_e = value.exp;
    value = nxt_diyfp_normalize(value);
    error <<= orig_e - value.exp;

    /*
     * Check whether the double's significand changes when the error is added
     * or substracted.
     */

    magnitude = NXT_DIYFP_SIGNIFICAND_SIZE + value.exp;
    prec_digits = NXT_DIYFP_SIGNIFICAND_SIZE - nxt_diyfp_sgnd_size(magnitude);

    if (prec_digits + NXT_DENOM_LOG >= NXT_DIYFP_SIGNIFICAND_SIZE) {
        /*
         * This can only happen for very small denormals. In this case the
         * half-way multiplied by the denominator exceeds the range of uint64.
         * Simply shift everything to the right.
         */
        shift = prec_digits + NXT_DENOM_LOG - NXT_DIYFP_SIGNIFICAND_SIZE + 1;

        value = nxt_diyfp_shift_right(value, shift);

        /*
         * Add 1 for the lost precision of error, and NXT_DENOM
         * for the lost precision of value.significand.
         */
        error = (error >> shift) + 1 + NXT_DENOM;
        prec_digits -= shift;
    }

    prec_bits = value.significand & (((uint64_t) 1 << prec_digits) - 1);
    prec_bits *= NXT_DENOM;

    half_way = (uint64_t) 1 << (prec_digits - 1);
    half_way *= NXT_DENOM;

    rounded = nxt_diyfp_shift_right(value, prec_digits);

    if (prec_bits >= half_way + error) {
        rounded.significand++;
    }

    return nxt_diyfp2d(rounded);
}


static double
nxt_strtod_internal(const u_char *start, size_t length, int exp)
{
    size_t        left, right;
    const u_char  *p, *e, *b;

    /* Trim leading zeroes. */

    p = start;
    e = p + length;

    while (p < e) {
        if (*p != '0') {
            start = p;
            break;
        }

        p++;
    }

    left = e - p;

    /* Trim trailing zeroes. */

    b = start;
    p = b + left - 1;

    while (p > b) {
        if (*p != '0') {
            break;
        }

        p--;
    }

    right = p - b + 1;

    length = right;

    if (length == 0) {
        return 0.0;
    }

    exp += (int) (left - right);

    if (exp + (int) length - 1 >= NXT_DECIMAL_POWER_MAX) {
        return INFINITY;
    }

    if (exp + (int) length <= NXT_DECIMAL_POWER_MIN) {
        return 0.0;
    }

    return nxt_diyfp_strtod(start, length, exp);
}


double
nxt_strtod(const u_char **start, const u_char *end)
{
    int           exponent, exp, insignf;
    u_char        c, *pos;
    nxt_bool_t    minus;
    const u_char  *e, *p, *last;
    u_char        data[128];

    exponent = 0;
    insignf = 0;

    pos = data;
    last = data + sizeof(data);

    for (p = *start; p < end; p++) {
        /* Values less than '0' become >= 208. */
        c = *p - '0';

        if (nxt_slow_path(c > 9)) {
            break;
        }

        if (pos < last) {
            *pos++ = *p;

        } else {
            insignf++;
        }
    }

    /* Do not emit a '.', but adjust the exponent instead. */
    if (p < end && *p == '.') {

        for (p++; p < end; p++) {
            /* Values less than '0' become >= 208. */
            c = *p - '0';

            if (nxt_slow_path(c > 9)) {
                break;
            }

            if (pos < last) {
                *pos++ = *p;
                exponent--;

            } else {
                /* Ignore insignificant digits in the fractional part. */
            }
        }
    }

    e = p + 1;

    if (e < end && (*p == 'e' || *p == 'E')) {
        minus = 0;

        if (e + 1 < end) {
            if (*e == '-') {
                e++;
                minus = 1;

            } else if (*e == '+') {
                e++;
            }
        }

        /* Values less than '0' become >= 208. */
        c = *e - '0';

        if (nxt_fast_path(c <= 9)) {
            exp = c;

            for (p = e + 1; p < end; p++) {
                /* Values less than '0' become >= 208. */
                c = *p - '0';

                if (nxt_slow_path(c > 9)) {
                    break;
                }

                exp = exp * 10 + c;
            }

            exponent += minus ? -exp : exp;
        }
    }

    *start = p;

    exponent += insignf;

    return nxt_strtod_internal(data, pos - data, exponent);
}
