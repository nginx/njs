
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 *
 * Grisu2 algorithm implementation for printing floating-point numbers based
 * upon the work of Milo Yip and Doug Currie.
 *
 * For algorithm information, see Loitsch, Florian. "Printing
 * floating-point numbers quickly and accurately with integers." ACM Sigplan
 * Notices 45.6 (2010): 233-243.
 *
 * Copyright (C) 2015 Doug Currie
 * based on dtoa_milo.h
 * Copyright (C) 2014 Milo Yip
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include <njs_main.h>
#include <njs_diyfp.h>
#include <njs_dtoa.h>


njs_inline void
njs_round(char *start, size_t length, uint64_t delta, uint64_t rest,
    uint64_t ten_kappa, uint64_t margin)
{
    while (rest < margin && delta - rest >= ten_kappa
           && (rest + ten_kappa < margin ||  /* closer */
               margin - rest > rest + ten_kappa - margin))
    {
        start[length - 1]--;
        rest += ten_kappa;
    }
}


njs_inline int
njs_dec_count(uint32_t n)
{
    if (n < 10000) {
        if (n < 100) {
            return (n < 10) ? 1 : 2;

        } else {
            return (n < 1000) ? 3 : 4;
        }

    } else {
        if (n < 1000000) {
            return (n < 100000) ? 5 : 6;

        } else {
            if (n < 100000000) {
                return (n < 10000000) ? 7 : 8;

            } else {
                return (n < 1000000000) ? 9 : 10;
            }
        }
    }
}


njs_inline size_t
njs_digit_gen(njs_diyfp_t v, njs_diyfp_t high, uint64_t delta, char *start,
    int *dec_exp)
{
    int          kappa;
    char         *p;
    uint32_t     integer, d;
    uint64_t     fraction, rest, margin;
    njs_diyfp_t  one;

    static const uint64_t pow10[] = {
        1,
        10,
        100,
        1000,
        10000,
        100000,
        1000000,
        10000000,
        100000000,
        1000000000
    };

    one = njs_diyfp((uint64_t) 1 << -high.exp, high.exp);
    integer = (uint32_t) (high.significand >> -one.exp);
    fraction = high.significand & (one.significand - 1);

    margin = njs_diyfp_sub(high, v).significand;

    p = start;

    kappa = njs_dec_count(integer);

    while (kappa > 0) {

        switch (kappa) {
        case 10: d = integer / 1000000000; integer %= 1000000000; break;
        case  9: d = integer /  100000000; integer %=  100000000; break;
        case  8: d = integer /   10000000; integer %=   10000000; break;
        case  7: d = integer /    1000000; integer %=    1000000; break;
        case  6: d = integer /     100000; integer %=     100000; break;
        case  5: d = integer /      10000; integer %=      10000; break;
        case  4: d = integer /       1000; integer %=       1000; break;
        case  3: d = integer /        100; integer %=        100; break;
        case  2: d = integer /         10; integer %=         10; break;
        default: d = integer;              integer =           0; break;
        }

        if (d != 0 || p != start) {
            *p++ = '0' + d;
        }

        kappa--;

        rest = ((uint64_t) integer << -one.exp) + fraction;

        if (rest < delta) {
            *dec_exp += kappa;
            njs_round(start, p - start, delta, rest, pow10[kappa] << -one.exp,
                      margin);
            return p - start;
        }
    }

    /* kappa = 0. */

    for ( ;; ) {
        fraction *= 10;
        delta *= 10;

        d = (uint32_t) (fraction >> -one.exp);

        if (d != 0 || p != start) {
            *p++ = '0' + d;
        }

        fraction &= one.significand - 1;
        kappa--;

        if (fraction < delta) {
            *dec_exp += kappa;
            margin *= (-kappa < 10) ? pow10[-kappa] : 0;
            njs_round(start, p - start, delta, fraction, one.significand,
                      margin);
            return p - start;
        }
    }
}


njs_inline njs_diyfp_t
njs_diyfp_normalize_boundary(njs_diyfp_t v)
{
    while ((v.significand & (NJS_DBL_HIDDEN_BIT << 1)) == 0) {
        v.significand <<= 1;
        v.exp--;
    }

    return njs_diyfp_shift_left(v, NJS_SIGNIFICAND_SHIFT - 2);
}


njs_inline void
njs_diyfp_normalize_boundaries(njs_diyfp_t v, njs_diyfp_t* minus,
    njs_diyfp_t* plus)
{
    njs_diyfp_t  pl, mi;

    pl = njs_diyfp_normalize_boundary(njs_diyfp((v.significand << 1) + 1,
                                                v.exp - 1));

    if (v.significand == NJS_DBL_HIDDEN_BIT) {
        mi = njs_diyfp((v.significand << 2) - 1, v.exp - 2);

    } else {
        mi = njs_diyfp((v.significand << 1) - 1, v.exp - 1);
    }

    mi.significand <<= mi.exp - pl.exp;
    mi.exp = pl.exp;

    *plus = pl;
    *minus = mi;
}


/*
 * Grisu2 produces optimal (shortest) decimal representation for 99.8%
 * of IEEE doubles. For remaining 0.2% bignum algorithm like Dragon4 is requred.
 */
njs_inline size_t
njs_grisu2(double value, char *start, int *dec_exp)
{
    njs_diyfp_t  v, low, high, ten_mk, scaled_v, scaled_low, scaled_high;

    v = njs_d2diyfp(value);

    njs_diyfp_normalize_boundaries(v, &low, &high);

    ten_mk = njs_cached_power_bin(high.exp, dec_exp);

    scaled_v = njs_diyfp_mul(njs_diyfp_normalize(v), ten_mk);
    scaled_low = njs_diyfp_mul(low, ten_mk);
    scaled_high = njs_diyfp_mul(high, ten_mk);

    scaled_low.significand++;
    scaled_high.significand--;

    return njs_digit_gen(scaled_v, scaled_high,
                         scaled_high.significand - scaled_low.significand,
                         start, dec_exp);
}


njs_inline size_t
njs_write_exponent(int exp, char *start)
{
    char      *p;
    size_t    length;
    uint32_t  u32;
    char      buf[4];

    /* -324 <= exp <= 308. */

    if (exp < 0) {
        *start++ = '-';
        exp = -exp;

    } else {
        *start++ = '+';
    }

    u32 = exp;
    p = buf + njs_length(buf);

    do {
        *--p = u32 % 10 + '0';
        u32 /= 10;
    } while (u32 != 0);

    length = buf + njs_length(buf) - p;

    memcpy(start, p, length);

    return length + 1;
}


njs_inline size_t
njs_dtoa_format(char *start, size_t len, int dec_exp)
{
    int     kk, offset, length;
    size_t  size;

    /* 10^(kk-1) <= v < 10^kk */

    length = (int) len;
    kk = length + dec_exp;

    if (length <= kk && kk <= 21) {

        /* 1234e7 -> 12340000000 */

        if (kk - length > 0) {
            njs_memset(&start[length], '0', kk - length);
        }

        return kk;

    } else if (0 < kk && kk <= 21) {

        /* 1234e-2 -> 12.34 */

        memmove(&start[kk + 1], &start[kk], length - kk);
        start[kk] = '.';

        return length + 1;

    } else if (-6 < kk && kk <= 0) {

        /* 1234e-6 -> 0.001234 */

        offset = 2 - kk;
        memmove(&start[offset], start, length);

        start[0] = '0';
        start[1] = '.';

        if (offset - 2 > 0) {
            njs_memset(&start[2], '0', offset - 2);
        }

        return length + offset;

    } else if (length == 1) {

        /* 1e30 */

        start[1] = 'e';

        size =  njs_write_exponent(kk - 1, &start[2]);

        return size + 2;

    }

    /* 1234e30 -> 1.234e33 */

    memmove(&start[2], &start[1], length - 1);
    start[1] = '.';
    start[length + 1] = 'e';

    size = njs_write_exponent(kk - 1, &start[length + 2]);

    return size + length + 2;
}


size_t
njs_dtoa(double value, char *start)
{
    int     dec_exp, minus;
    char    *p;
    size_t  length;

    /* Not handling NaN and inf. */

    minus = 0;
    p = start;

    if (value == 0) {
        *p++ = '0';

        return p - start;
    }

    if (signbit(value)) {
        *p++ = '-';
        value = -value;
        minus = 1;
    }

    length = njs_grisu2(value, p, &dec_exp);

    return njs_dtoa_format(p, length, dec_exp) + minus;
}
