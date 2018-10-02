
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

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_diyfp.h>
#include <nxt_dtoa.h>

#include <math.h>
#include <string.h>


nxt_inline void
nxt_grisu2_round(char *start, size_t len, uint64_t delta, uint64_t rest,
    uint64_t ten_kappa, uint64_t wp_w)
{
    while (rest < wp_w && delta - rest >= ten_kappa
           && (rest + ten_kappa < wp_w ||  /* closer */
               wp_w - rest > rest + ten_kappa - wp_w))
    {
        start[len - 1]--;
        rest += ten_kappa;
    }
}


nxt_inline int
nxt_dec_count(uint32_t n)
{
    if (n < 10) return 1;
    if (n < 100) return 2;
    if (n < 1000) return 3;
    if (n < 10000) return 4;
    if (n < 100000) return 5;
    if (n < 1000000) return 6;
    if (n < 10000000) return 7;
    if (n < 100000000) return 8;
    if (n < 1000000000) return 9;

    return 10;
}


nxt_inline size_t
nxt_grisu2_gen(nxt_diyfp_t W, nxt_diyfp_t Mp, uint64_t delta, char *start,
    int *dec_exp)
{
    int          kappa;
    char         c, *p;
    uint32_t     p1, d;
    uint64_t     p2, tmp;
    nxt_diyfp_t  one, wp_w;

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

    wp_w = nxt_diyfp_sub(Mp, W);

    one = nxt_diyfp((uint64_t) 1 << -Mp.exp, Mp.exp);
    p1 = (uint32_t) (Mp.significand >> -one.exp);
    p2 = Mp.significand & (one.significand - 1);

    p = start;

    /* GCC 4.2 complains about uninitialized d. */
    d = 0;

    kappa = nxt_dec_count(p1);

    while (kappa > 0) {

        switch (kappa) {
            case 10: d = p1 / 1000000000; p1 %= 1000000000; break;
            case  9: d = p1 /  100000000; p1 %=  100000000; break;
            case  8: d = p1 /   10000000; p1 %=   10000000; break;
            case  7: d = p1 /    1000000; p1 %=    1000000; break;
            case  6: d = p1 /     100000; p1 %=     100000; break;
            case  5: d = p1 /      10000; p1 %=      10000; break;
            case  4: d = p1 /       1000; p1 %=       1000; break;
            case  3: d = p1 /        100; p1 %=        100; break;
            case  2: d = p1 /         10; p1 %=         10; break;
            case  1: d = p1;              p1 =           0; break;
            default:
                nxt_unreachable();
        }

        if (d != 0 || p != start) {
            *p++ = '0' + d;
        }

        kappa--;

        tmp = ((uint64_t) p1 << -one.exp) + p2;

        if (tmp <= delta) {
            *dec_exp += kappa;
            nxt_grisu2_round(start, p - start, delta, tmp,
                             pow10[kappa] << -one.exp, wp_w.significand);
            return p - start;
        }
    }

    /* kappa = 0. */

    for ( ;; ) {
        p2 *= 10;
        delta *= 10;
        c = (char) (p2 >> -one.exp);

        if (c != 0 || p != start) {
            *p++ = '0' + c;
        }

        p2 &= one.significand - 1;
        kappa--;

        if (p2 < delta) {
            *dec_exp += kappa;
            tmp = (-kappa < 10) ? pow10[-kappa] : 0;
            nxt_grisu2_round(start, p - start, delta, p2, one.significand,
                             wp_w.significand * tmp);
            break;
        }
    }

    return p - start;
}


nxt_inline nxt_diyfp_t
nxt_diyfp_normalize_boundary(nxt_diyfp_t v)
{
    while ((v.significand & (NXT_DBL_HIDDEN_BIT << 1)) == 0) {
        v.significand <<= 1;
        v.exp--;
    }

    return nxt_diyfp_shift_left(v, NXT_SIGNIFICAND_SHIFT - 2);
}


nxt_inline void
nxt_diyfp_normalize_boundaries(nxt_diyfp_t v, nxt_diyfp_t* minus,
    nxt_diyfp_t* plus)
{
    nxt_diyfp_t  pl, mi;

    pl = nxt_diyfp_normalize_boundary(nxt_diyfp((v.significand << 1) + 1,
                                                v.exp - 1));

    if (v.significand == NXT_DBL_HIDDEN_BIT) {
        mi = nxt_diyfp((v.significand << 2) - 1, v.exp - 2);

    } else {
        mi = nxt_diyfp((v.significand << 1) - 1, v.exp - 1);
    }

    mi.significand <<= mi.exp - pl.exp;
    mi.exp = pl.exp;

    *plus = pl;
    *minus = mi;
}


nxt_inline size_t
nxt_grisu2(double value, char *start, int *dec_exp)
{
    nxt_diyfp_t  v, w_m, w_p, c_mk, W, Wp, Wm;

    v = nxt_d2diyfp(value);

    nxt_diyfp_normalize_boundaries(v, &w_m, &w_p);

    c_mk = nxt_cached_power_bin(w_p.exp, dec_exp);
    W = nxt_diyfp_mul(nxt_diyfp_normalize(v), c_mk);

    Wp = nxt_diyfp_mul(w_p, c_mk);
    Wm = nxt_diyfp_mul(w_m, c_mk);

    Wm.significand++;
    Wp.significand--;

   return nxt_grisu2_gen(W, Wp, Wp.significand - Wm.significand, start,
                         dec_exp);
}


nxt_inline size_t
nxt_write_exponent(int exp, char* start)
{
    char      *p;
    size_t    len;
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
    p = buf + nxt_length(buf);

    do {
        *--p = u32 % 10 + '0';
        u32 /= 10;
    } while (u32 != 0);

    len = buf + nxt_length(buf) - p;

    memcpy(start, p, len);

    return len + 1;
}


nxt_inline size_t
nxt_prettify(char *start, size_t len, int dec_exp)
{
    int     kk, offset, length;
    size_t  size;

    /* 10^(kk-1) <= v < 10^kk */

    length = (int) len;
    kk = length + dec_exp;

    if (length <= kk && kk <= 21) {

        /* 1234e7 -> 12340000000 */

        if (kk - length > 0) {
            nxt_memset(&start[length], '0', kk - length);
        }

        return kk;

    } else if (0 < kk && kk <= 21) {

        /* 1234e-2 -> 12.34 */

        memmove(&start[kk + 1], &start[kk], length - kk);
        start[kk] = '.';

        return (length + 1);

    } else if (-6 < kk && kk <= 0) {

        /* 1234e-6 -> 0.001234 */

        offset = 2 - kk;
        memmove(&start[offset], start, length);

        start[0] = '0';
        start[1] = '.';

        if (offset - 2 > 0) {
            nxt_memset(&start[2], '0', offset - 2);
        }

        return (length + offset);

    } else if (length == 1) {

        /* 1e30 */

        start[1] = 'e';

        size =  nxt_write_exponent(kk - 1, &start[2]);

        return (size + 2);

    }

    /* 1234e30 -> 1.234e33 */

    memmove(&start[2], &start[1], length - 1);
    start[1] = '.';
    start[length + 1] = 'e';

    size = nxt_write_exponent(kk - 1, &start[length + 2]);

    return (size + length + 2);
}


size_t
nxt_dtoa(double value, char *start)
{
    int     dec_exp, minus;
    char    *p;
    size_t  length;

    /* Not handling NaN and inf. */

    minus = 0;
    p = start;

    if (value == 0) {
        *p++ = '0';

        return (p - start);
    }

    if (signbit(value)) {
        *p++ = '-';
        value = -value;
        minus = 1;
    }

    length = nxt_grisu2(value, p, &dec_exp);

    length = nxt_prettify(p, length, dec_exp);

    return (minus + length);
}
