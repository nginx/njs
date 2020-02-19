
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


njs_inline void
njs_round_prec(char *start, size_t length, uint64_t rest, uint64_t ten_kappa,
    uint64_t unit, int *kappa)
{
    njs_int_t  i;

    if (unit >= ten_kappa || ten_kappa - unit <= unit) {
        return;
    }

    if ((ten_kappa - rest > rest) && (ten_kappa - 2 * rest >= 2 * unit)) {
        return;
    }

    if ((rest > unit) && (ten_kappa - (rest - unit) <= (rest - unit))) {
        start[length - 1]++;

        for (i = length - 1; i > 0; --i) {
            if (start[i] != '0' + 10) {
                break;
            }

            start[i] = '0';
            start[i - 1]++;
        }

        if (start[0] == '0' + 10) {
            start[0] = '1';
            *kappa += 1;
        }
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


njs_inline size_t
njs_digit_gen_prec(njs_diyfp_t v, size_t prec, char *start, int *dec_exp)
{
    int          kappa;
    char         *p;
    uint32_t     integer, divisor;
    uint64_t     fraction, rest, error;
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

    one = njs_diyfp((uint64_t) 1 << -v.exp, v.exp);
    integer = (uint32_t) (v.significand >> -one.exp);
    fraction = v.significand & (one.significand - 1);

    error = 1;

    p = start;

    kappa = njs_dec_count(integer);

    while (kappa > 0) {
        divisor = pow10[kappa - 1];

        *p++ = '0' + integer / divisor;

        integer %= divisor;

        kappa--;
        prec--;

        if (prec == 0) {
            rest = ((uint64_t) integer << -one.exp) + fraction;
            njs_round_prec(start, p - start, rest, pow10[kappa] << -one.exp,
                           error, &kappa);

            *dec_exp += kappa;
            return p - start;
        }
    }

    /* kappa = 0. */

    while (prec > 0 && fraction > error) {
        fraction *= 10;
        error *= 10;

        *p++ = '0' + (fraction >> -one.exp);

        fraction &= one.significand - 1;
        kappa--;
        prec--;
    }

    njs_round_prec(start, p - start, fraction, one.significand, error, &kappa);

    *dec_exp += kappa;

    return p - start;
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
njs_grisu2(double value, char *start, int *point)
{
    int          dec_exp;
    size_t       length;
    njs_diyfp_t  v, low, high, ten_mk, scaled_v, scaled_low, scaled_high;

    v = njs_d2diyfp(value);

    njs_diyfp_normalize_boundaries(v, &low, &high);

    ten_mk = njs_cached_power_bin(high.exp, &dec_exp);

    scaled_v = njs_diyfp_mul(njs_diyfp_normalize(v), ten_mk);
    scaled_low = njs_diyfp_mul(low, ten_mk);
    scaled_high = njs_diyfp_mul(high, ten_mk);

    scaled_low.significand++;
    scaled_high.significand--;

    length = njs_digit_gen(scaled_v, scaled_high,
                         scaled_high.significand - scaled_low.significand,
                         start, &dec_exp);

    *point = length + dec_exp;

    return length;
}


njs_inline size_t
njs_grisu2_prec(double value, char *start, size_t prec, int *point)
{
    int          dec_exp;
    size_t       length;
    njs_diyfp_t  v, ten_mk, scaled_v;

    v = njs_diyfp_normalize(njs_d2diyfp(value));

    ten_mk = njs_cached_power_bin(v.exp, &dec_exp);

    scaled_v = njs_diyfp_mul(v, ten_mk);

    length = njs_digit_gen_prec(scaled_v, prec, start, &dec_exp);

    *point = length + dec_exp;

    return length;
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
njs_dtoa_format(char *start, size_t len, int point)
{
    int     offset, length;
    size_t  size;

    length = (int) len;

    if (length <= point && point <= 21) {

        /* 1234e7 -> 12340000000 */

        if (point - length > 0) {
            njs_memset(&start[length], '0', point - length);
        }

        return point;
    }

    if (0 < point && point <= 21) {

        /* 1234e-2 -> 12.34 */

        memmove(&start[point + 1], &start[point], length - point);
        start[point] = '.';

        return length + 1;
    }

    if (-6 < point && point <= 0) {

        /* 1234e-6 -> 0.001234 */

        offset = 2 - point;
        memmove(&start[offset], start, length);

        start[0] = '0';
        start[1] = '.';

        if (offset - 2 > 0) {
            njs_memset(&start[2], '0', offset - 2);
        }

        return length + offset;
    }

    /* 1234e30 -> 1.234e33 */

    if (length == 1) {

        /* 1e30 */

        start[1] = 'e';

        size =  njs_write_exponent(point - 1, &start[2]);

        return size + 2;
    }

    memmove(&start[2], &start[1], length - 1);
    start[1] = '.';
    start[length + 1] = 'e';

    size = njs_write_exponent(point - 1, &start[length + 2]);

    return size + length + 2;
}


njs_inline size_t
njs_dtoa_exp_format(char *start, int exponent, size_t prec, size_t len)
{
    char  *p;

    p = &start[len];
    if (prec != 1) {
        memmove(&start[2], &start[1], len - 1);
        start[1] = '.';
        p++;
    }

    njs_memset(p, '0', prec - len);
    p += prec - len;

    *p++ = 'e';

    return prec + 1 + (prec != 1) + njs_write_exponent(exponent, p);
}


njs_inline size_t
njs_dtoa_prec_format(char *start, size_t prec, size_t len, int point)
{
    int     exponent;
    size_t  m, rest;

    exponent = point - 1;

    if (exponent < -6 || exponent >= (int) prec) {
        return njs_dtoa_exp_format(start, exponent, prec, len);
    }

    /* Fixed notation. */

    if (point <= 0) {

        /* 1234e-2 => 0.001234000 */

        memmove(&start[2 + (-point)], start, len);
        start[0] = '0';
        start[1] = '.';

        njs_memset(&start[2], '0', -point);

        if (prec > len) {
            njs_memset(&start[2 + (-point) + len], '0', prec - len);
        }

        return prec + 2 + (-point);
    }

    if (point >= (int) len) {

        /* TODO: (2**96).toPrecision(45) not enough precision, BigInt needed. */

        njs_memset(&start[len], '0', point - len);

        if (point < (int) prec) {
            start[point] = '.';

            njs_memset(&start[point + 1], '0', prec - point);
        }

    } else if (point < (int) prec) {

        /* 123456 -> 123.45600 */

        m = njs_min((int) len, point);
        rest = njs_min(len, prec) - m;
        memmove(&start[m + 1], &start[m], rest);

        start[m] = '.';

        njs_memset(&start[m + rest + 1], '0', prec - m - rest);
    }

    return prec + (point < (int) prec);
}


size_t
njs_dtoa(double value, char *start)
{
    int     point, minus;
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

    length = njs_grisu2(value, p, &point);

    return njs_dtoa_format(p, length, point) + minus;
}


/*
 * TODO: For prec > 16 result maybe rounded. To support prec > 16 Bignum
 * support is requred.
 */
size_t
njs_dtoa_precision(double value, char *start, size_t prec)
{
    int     point, minus;
    char    *p;
    size_t  length;

    /* Not handling NaN and inf. */

    p = start;
    minus = 0;

    if (value != 0) {
        if (value < 0) {
            *p++ = '-';
            value = -value;
            minus = 1;
        }

        length = njs_grisu2_prec(value, p, prec, &point);

    } else {
        start[0] = '0';
        length = 1;
        point = 1;
    }

    return njs_dtoa_prec_format(p, prec, length, point) + minus;
}


size_t
njs_dtoa_exponential(double value, char *start, njs_int_t frac)
{
    int     point, minus;
    char    *p;
    size_t  length;

    /* Not handling NaN and inf. */

    p = start;
    minus = 0;

    if (value != 0) {
        if (value < 0) {
            *p++ = '-';
            value = -value;
            minus = 1;
        }

        if (frac == -1) {
            length = njs_grisu2(value, p, &point);

        } else {
            length = njs_grisu2_prec(value, p, frac + 1, &point);
        }

    } else {
        start[0] = '0';
        length = 1;
        point = 1;
    }

    if (frac == -1) {
        frac = length - 1;
    }

    return njs_dtoa_exp_format(p, point - 1, frac + 1, length) + minus;
}
