
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 *
 * An internal fixed_dtoa() implementation based upon V8
 * src/numbers/fixed-dtoa.cc without bignum support.
 *
 * Copyright 2011 the V8 project authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 */

#include <njs_main.h>
#include <njs_diyfp.h>


typedef struct {
    uint64_t  high;
    uint64_t  low;
} njs_diyu128_t;


#define njs_diyu128(_h, _l)       (njs_diyu128_t) { .high = (_h), .low = (_l) }


njs_inline njs_diyu128_t
njs_diyu128_mul(njs_diyu128_t v, uint32_t multiplicand)
{
    uint32_t  part;
    uint64_t  accumulator;

    accumulator = (v.low & UINT32_MAX) * multiplicand;
    part = (uint32_t) (accumulator & UINT32_MAX);

    accumulator >>= 32;
    accumulator = accumulator + (v.low >> 32) * multiplicand;
    v.low = (accumulator << 32) + part;

    accumulator >>= 32;
    accumulator = accumulator + (v.high & UINT32_MAX) * multiplicand;
    part = (uint32_t) (accumulator & UINT32_MAX);

    accumulator >>= 32;
    accumulator = accumulator + (v.high >> 32) * multiplicand;
    v.high = (accumulator << 32) + part;

    return v;
}


njs_inline njs_diyu128_t
njs_diyu128_shift(njs_diyu128_t v, njs_int_t shift)
{
    /* -64 <= shift <= 64.*/

    if (shift < 0) {
        if (shift == -64) {
            v.high = v.low;
            v.low = 0;

        } else {
            v.high <<= -shift;
            v.high += v.low >> (64 + shift);
            v.low <<= -shift;
        }

        return v;
    }

    if (shift > 0) {
        if (shift == 64) {
            v.low = v.high;
            v.high = 0;

        } else {
            v.low >>= shift;
            v.low += v.high << (64 - shift);
            v.high >>= shift;
        }
    }

    return v;
}


njs_inline njs_int_t
njs_diyu128_div_mod_pow2(njs_diyu128_t *v, njs_int_t power)
{
    uint64_t   part_low, part_high;
    njs_int_t  result;

    if (power >= 64) {
        result = (int) (v->high >> (power - 64));
        v->high -= (uint64_t) result << (power - 64);

        return result;
    }

    part_low = v->low >> power;
    part_high = v->high << (64 - power);

    result = (int) (part_low + part_high);

    v->low -= part_low << power;
    v->high = 0;

    return result;
}


njs_inline njs_bool_t
njs_diyu128_is_zero(njs_diyu128_t v)
{
    if (v.low == 0 && v.high == 0) {
        return 1;
    }

    return 0;
}


njs_inline njs_uint_t
njs_diyu128_bit_at(njs_diyu128_t v, njs_uint_t pos)
{
    if (pos >= 64) {
        return (njs_uint_t) (v.high >> (pos - 64)) & 1;
    }

    return (njs_uint_t) (v.low >> pos) & 1;
}


static size_t
njs_fill_digits32(uint32_t number, char *start, size_t length)
{
    char       c;
    size_t     i, j, n;
    njs_int_t  digit;

    n = 0;

    while (number != 0) {
        digit = number % 10;
        number /= 10;
        start[length + n] = '0' + digit;
        n++;
    }

    i = length;
    j = length + n - 1;

    while (i < j) {
        c = start[i];
        start[i] = start[j];
        start[j] = c;

        i++;
        j--;
    }

    return length + n;
}


njs_inline size_t
njs_fill_digits32_fixed_length(uint32_t number, size_t requested_length,
    char *start, size_t length)
{
    size_t  i;

    i = requested_length;

    while (i-- > 0) {
        start[length + i] = '0' + number % 10;
        number /= 10;
    }

    return length + requested_length;
}


njs_inline size_t
njs_fill_digits64(uint64_t number, char *start, size_t length)
{
    uint32_t  part0, part1, part2, ten7;

    ten7 = 10000000;

    part2 = (uint32_t) (number % ten7);
    number /= ten7;

    part1 = (uint32_t) (number % ten7);
    part0 = (uint32_t) (number / ten7);

    if (part0 != 0) {
        length = njs_fill_digits32(part0, start, length);
        length = njs_fill_digits32_fixed_length(part1, 7, start, length);
        return njs_fill_digits32_fixed_length(part2, 7, start, length);
    }

    if (part1 != 0) {
        length = njs_fill_digits32(part1, start, length);
        return njs_fill_digits32_fixed_length(part2, 7, start, length);
    }

    return njs_fill_digits32(part2, start, length);
}


njs_inline size_t
njs_fill_digits64_fixed_length(uint64_t number, char *start, size_t length)
{
    uint32_t  part0, part1, part2, ten7;

    ten7 = 10000000;

    part2 = (uint32_t) (number % ten7);
    number /= ten7;

    part1 = (uint32_t) (number % ten7);
    part0 = (uint32_t) (number / ten7);

    length = njs_fill_digits32_fixed_length(part0, 3, start, length);
    length = njs_fill_digits32_fixed_length(part1, 7, start, length);

    return njs_fill_digits32_fixed_length(part2, 7, start, length);
}


njs_inline size_t
njs_dtoa_round_up(char *start, size_t length, njs_int_t *point)
{
    size_t  i;

    if (length == 0) {
        start[0] = '1';
        *point = 1;
        return 1;
    }

    start[length - 1]++;

    for (i = length - 1; i > 0; --i) {
        if (start[i] != '0' + 10) {
            return length;
        }

        start[i] = '0';
        start[i - 1]++;
    }

    if (start[0] == '0' + 10) {
        start[0] = '1';
        (*point)++;
    }

    return length;
}


static size_t
njs_fill_fractionals(uint64_t fractionals, int exponent, njs_uint_t frac,
    char *start, size_t length, njs_int_t *point)
{
    njs_int_t      n, digit;
    njs_uint_t     i;
    njs_diyu128_t  fractionals128;

    /*
     * -128 <= exponent <= 0.
     * 0 <= fractionals * 2^exponent < 1.
     */

    if (-exponent <= 64) {
        /* fractionals <= 2^56. */

        n = -exponent;

        for (i = 0; i < frac && fractionals != 0; ++i) {
            /*
             * Multiplication by 10 is replaced with multiplication by 5 and
             * point location adjustment. To avoid integer-overflow.
             */

            fractionals *= 5;
            n--;

            digit = (njs_int_t) (fractionals >> n);
            fractionals -= (uint64_t) digit << n;

            start[length++] = '0' + digit;
        }

        if (n > 0 && ((fractionals >> (n - 1)) & 1)) {
            length = njs_dtoa_round_up(start, length, point);
        }

    } else {

        fractionals128 = njs_diyu128(fractionals, 0);
        fractionals128 = njs_diyu128_shift(fractionals128, -exponent - 64);

        n = 128;

        for (i = 0; i < frac && !njs_diyu128_is_zero(fractionals128); ++i) {
            /*
             * Multiplication by 10 is replaced with multiplication by 5 and
             * point location adjustment. To avoid integer-overflow.
             */

            fractionals128 = njs_diyu128_mul(fractionals128, 5);
            n--;

            digit = njs_diyu128_div_mod_pow2(&fractionals128, n);

            start[length++] = '0' + digit;
        }

        if (njs_diyu128_bit_at(fractionals128, n - 1)) {
            length = njs_dtoa_round_up(start, length, point);
        }
    }

    return length;
}


njs_inline size_t
njs_trim_zeroes(char *start, size_t length, njs_int_t *point)
{
    size_t  i, n;

    while (length > 0 && start[length - 1] == '0') {
        length--;
    }

    n = 0;

    while (n < length && start[n] == '0') {
        n++;
    }

    if (n != 0) {
        for (i = n; i < length; ++i) {
            start[i - n] = start[i];
        }

        length -= n;
        *point -= n;
    }

    return length;
}


size_t
njs_fixed_dtoa(double value, njs_uint_t frac, char *start, njs_int_t *point)
{
    size_t       length;
    uint32_t     quotient;
    uint64_t     significand, divisor, dividend, remainder, integral, fract;
    njs_int_t    exponent;
    njs_diyfp_t  v;

    length = 0;
    v = njs_d2diyfp(value);

    significand = v.significand;
    exponent = v.exp;

    /* exponent <= 19. */

    if (exponent + NJS_SIGNIFICAND_SIZE > 64) {
        /* exponent > 11. */

        divisor = njs_uint64(0xB1, 0xA2BC2EC5); /* 5 ^ 17 */

        dividend = significand;

        if (exponent > 17) {
            /* (e - 17) <= 3. */
            dividend <<= exponent - 17;

            quotient = (uint32_t) (dividend / divisor);
            remainder = (dividend % divisor) << 17;

        } else {
            divisor <<= 17 - exponent;

            quotient = (uint32_t) (dividend / divisor);
            remainder = (dividend % divisor) << exponent;
        }

        length = njs_fill_digits32(quotient, start, length);
        length = njs_fill_digits64_fixed_length(remainder, start, length);
        *point = (njs_int_t) length;

    } else if (exponent >= 0) {
        /* 0 <= exponent <= 11. */

        significand <<= exponent;
        length = njs_fill_digits64(significand, start, length);
        *point = (njs_int_t) length;

    } else if (exponent > -NJS_SIGNIFICAND_SIZE) {
        /* -53 < exponent < 0. */

        integral = significand >> -exponent;
        fract = significand - (integral << -exponent);

        if (integral > UINT32_MAX) {
            length = njs_fill_digits64(integral, start, length);

        } else {
            length = njs_fill_digits32((uint32_t) integral, start, length);
        }

        *point = (njs_int_t) length;
        length = njs_fill_fractionals(fract, exponent, frac, start, length,
                                      point);

    } else if (exponent < -128) {
        /* Valid for frac =< 20 only. TODO: bignum support. */

        start[0] = '\0';

        *point = -frac;

    } else {
        /* -128 <= exponent <= -53. */

        *point = 0;
        length = njs_fill_fractionals(significand, exponent, frac, start,
                                      length, point);
    }

    length = njs_trim_zeroes(start, length, point);
    start[length] = '\0';

    if (length == 0) {
        *point = -frac;
    }

    return length;
}
