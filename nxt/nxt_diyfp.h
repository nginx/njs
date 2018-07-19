
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 *
 * An internal diy_fp implementation.
 * For details, see Loitsch, Florian. "Printing floating-point numbers quickly
 * and accurately with integers." ACM Sigplan Notices 45.6 (2010): 233-243.
 */

#ifndef _NXT_DIYFP_H_INCLUDED_
#define _NXT_DIYFP_H_INCLUDED_

#include <nxt_types.h>
#include <math.h>


typedef struct {
    uint64_t    significand;
    int         exp;
} nxt_diyfp_t;


#define nxt_diyfp(_s, _e)           (nxt_diyfp_t) \
                                    { .significand = (_s), .exp = (_e) }
#define nxt_uint64(h, l)            (((uint64_t) (h) << 32) + (l))


#define NXT_DBL_SIGNIFICAND_SIZE    52
#define NXT_DBL_EXPONENT_BIAS       (0x3FF + NXT_DBL_SIGNIFICAND_SIZE)
#define NXT_DBL_EXPONENT_MIN        (-NXT_DBL_EXPONENT_BIAS)
#define NXT_DBL_EXPONENT_MAX        (0x7FF - NXT_DBL_EXPONENT_BIAS)
#define NXT_DBL_EXPONENT_DENORMAL   (-NXT_DBL_EXPONENT_BIAS + 1)

#define NXT_DBL_SIGNIFICAND_MASK    nxt_uint64(0x000FFFFF, 0xFFFFFFFF)
#define NXT_DBL_HIDDEN_BIT          nxt_uint64(0x00100000, 0x00000000)
#define NXT_DBL_EXPONENT_MASK       nxt_uint64(0x7FF00000, 0x00000000)

#define NXT_DIYFP_SIGNIFICAND_SIZE  64

#define NXT_SIGNIFICAND_SIZE        53
#define NXT_SIGNIFICAND_SHIFT       (NXT_DIYFP_SIGNIFICAND_SIZE     \
                                     - NXT_DBL_SIGNIFICAND_SIZE)

#define NXT_DECIMAL_EXPONENT_OFF    348
#define NXT_DECIMAL_EXPONENT_MIN    (-348)
#define NXT_DECIMAL_EXPONENT_MAX    340
#define NXT_DECIMAL_EXPONENT_DIST   8


nxt_inline nxt_diyfp_t
nxt_d2diyfp(double d)
{
    int           biased_exp;
    uint64_t      significand;
    nxt_diyfp_t   r;

    union {
        double    d;
        uint64_t  u64;
    } u;

    u.d = d;

    biased_exp = (u.u64 & NXT_DBL_EXPONENT_MASK) >> NXT_DBL_SIGNIFICAND_SIZE;
    significand = u.u64 & NXT_DBL_SIGNIFICAND_MASK;

    if (biased_exp != 0) {
        r.significand = significand + NXT_DBL_HIDDEN_BIT;
        r.exp = biased_exp - NXT_DBL_EXPONENT_BIAS;

    } else {
        r.significand = significand;
        r.exp = NXT_DBL_EXPONENT_MIN + 1;
    }

    return r;
}


nxt_inline double
nxt_diyfp2d(nxt_diyfp_t v)
{
    int           exp;
    uint64_t      significand, biased_exp;

    union {
        double    d;
        uint64_t  u64;
    } u;

    exp = v.exp;
    significand = v.significand;

    while (significand > NXT_DBL_HIDDEN_BIT + NXT_DBL_SIGNIFICAND_MASK) {
        significand >>= 1;
        exp++;
    }

    if (exp >= NXT_DBL_EXPONENT_MAX) {
        return INFINITY;
    }

    if (exp < NXT_DBL_EXPONENT_DENORMAL) {
        return 0.0;
    }

    while (exp > NXT_DBL_EXPONENT_DENORMAL
           && (significand & NXT_DBL_HIDDEN_BIT) == 0)
    {
        significand <<= 1;
        exp--;
    }

    if (exp == NXT_DBL_EXPONENT_DENORMAL
        && (significand & NXT_DBL_HIDDEN_BIT) == 0)
    {
        biased_exp = 0;

    } else {
        biased_exp = (uint64_t) (exp + NXT_DBL_EXPONENT_BIAS);
    }

    u.u64 = (significand & NXT_DBL_SIGNIFICAND_MASK)
            | (biased_exp << NXT_DBL_SIGNIFICAND_SIZE);

    return u.d;
}


nxt_inline nxt_diyfp_t
nxt_diyfp_shift_left(nxt_diyfp_t v, unsigned shift)
{
    return nxt_diyfp(v.significand << shift, v.exp - shift);
}


nxt_inline nxt_diyfp_t
nxt_diyfp_shift_right(nxt_diyfp_t v, unsigned shift)
{
    return nxt_diyfp(v.significand >> shift, v.exp + shift);
}


nxt_inline nxt_diyfp_t
nxt_diyfp_sub(nxt_diyfp_t lhs, nxt_diyfp_t rhs)
{
    return nxt_diyfp(lhs.significand - rhs.significand, lhs.exp);
}


nxt_inline nxt_diyfp_t
nxt_diyfp_mul(nxt_diyfp_t lhs, nxt_diyfp_t rhs)
{
#if (NXT_HAVE_UNSIGNED_INT128)

    uint64_t       l, h;
    nxt_uint128_t  u128;

    u128 = (nxt_uint128_t) (lhs.significand)
           * (nxt_uint128_t) (rhs.significand);

    h = u128 >> 64;
    l = (uint64_t) u128;

    /* rounding. */

    if (l & ((uint64_t) 1 << 63)) {
        h++;
    }

    return nxt_diyfp(h, lhs.exp + rhs.exp + 64);

#else

    uint64_t  a, b, c, d, ac, bc, ad, bd, tmp;

    a = lhs.significand >> 32;
    b = lhs.significand & 0xffffffff;
    c = rhs.significand >> 32;
    d = rhs.significand & 0xffffffff;

    ac = a * c;
    bc = b * c;
    ad = a * d;
    bd = b * d;

    tmp = (bd >> 32) + (ad & 0xffffffff) + (bc & 0xffffffff);

    /* mult_round. */

    tmp += 1U << 31;

    return nxt_diyfp(ac + (ad >> 32) + (bc >> 32) + (tmp >> 32),
                     lhs.exp + rhs.exp + 64);

#endif
}


nxt_inline nxt_diyfp_t
nxt_diyfp_normalize(nxt_diyfp_t v)
{
    return nxt_diyfp_shift_left(v, nxt_leading_zeros64(v.significand));
}


nxt_diyfp_t nxt_cached_power_dec(int exp, int *dec_exp);
nxt_diyfp_t nxt_cached_power_bin(int exp, int *dec_exp);


#endif /* _NXT_DIYFP_H_INCLUDED_ */
