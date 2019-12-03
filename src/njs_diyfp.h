
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 *
 * An internal diy_fp implementation.
 * For details, see Loitsch, Florian. "Printing floating-point numbers quickly
 * and accurately with integers." ACM Sigplan Notices 45.6 (2010): 233-243.
 */

#ifndef _NJS_DIYFP_H_INCLUDED_
#define _NJS_DIYFP_H_INCLUDED_


typedef struct {
    uint64_t    significand;
    int         exp;
} njs_diyfp_t;


typedef union {
    double      d;
    uint64_t    u64;
} njs_diyfp_conv_t;


#define njs_diyfp(_s, _e)           (njs_diyfp_t) \
                                    { .significand = (_s), .exp = (_e) }
#define njs_uint64(h, l)            (((uint64_t) (h) << 32) + (l))


#define NJS_DBL_SIGNIFICAND_SIZE    52
#define NJS_DBL_EXPONENT_OFFSET     ((int64_t) 0x3ff)
#define NJS_DBL_EXPONENT_BIAS       (NJS_DBL_EXPONENT_OFFSET                  \
                                     + NJS_DBL_SIGNIFICAND_SIZE)
#define NJS_DBL_EXPONENT_MIN        (-NJS_DBL_EXPONENT_BIAS)
#define NJS_DBL_EXPONENT_MAX        (0x7ff - NJS_DBL_EXPONENT_BIAS)
#define NJS_DBL_EXPONENT_DENORMAL   (-NJS_DBL_EXPONENT_BIAS + 1)

#define NJS_DBL_SIGNIFICAND_MASK    njs_uint64(0x000FFFFF, 0xFFFFFFFF)
#define NJS_DBL_HIDDEN_BIT          njs_uint64(0x00100000, 0x00000000)
#define NJS_DBL_EXPONENT_MASK       njs_uint64(0x7FF00000, 0x00000000)
#define NJS_DBL_SIGN_MASK           njs_uint64(0x80000000, 0x00000000)

#define NJS_DIYFP_SIGNIFICAND_SIZE  64

#define NJS_SIGNIFICAND_SIZE        53
#define NJS_SIGNIFICAND_SHIFT       (NJS_DIYFP_SIGNIFICAND_SIZE               \
                                     - NJS_DBL_SIGNIFICAND_SIZE)

#define NJS_DECIMAL_EXPONENT_OFF    348
#define NJS_DECIMAL_EXPONENT_MIN    (-348)
#define NJS_DECIMAL_EXPONENT_MAX    340
#define NJS_DECIMAL_EXPONENT_DIST   8


njs_inline njs_diyfp_t
njs_d2diyfp(double d)
{
    int           biased_exp;
    uint64_t      significand;
    njs_diyfp_t   r;

    union {
        double    d;
        uint64_t  u64;
    } u;

    u.d = d;

    biased_exp = (u.u64 & NJS_DBL_EXPONENT_MASK) >> NJS_DBL_SIGNIFICAND_SIZE;
    significand = u.u64 & NJS_DBL_SIGNIFICAND_MASK;

    if (biased_exp != 0) {
        r.significand = significand + NJS_DBL_HIDDEN_BIT;
        r.exp = biased_exp - NJS_DBL_EXPONENT_BIAS;

    } else {
        r.significand = significand;
        r.exp = NJS_DBL_EXPONENT_MIN + 1;
    }

    return r;
}


njs_inline double
njs_diyfp2d(njs_diyfp_t v)
{
    int               exp;
    uint64_t          significand, biased_exp;
    njs_diyfp_conv_t  conv;

    exp = v.exp;
    significand = v.significand;

    while (significand > NJS_DBL_HIDDEN_BIT + NJS_DBL_SIGNIFICAND_MASK) {
        significand >>= 1;
        exp++;
    }

    if (exp >= NJS_DBL_EXPONENT_MAX) {
        return INFINITY;
    }

    if (exp < NJS_DBL_EXPONENT_DENORMAL) {
        return 0.0;
    }

    while (exp > NJS_DBL_EXPONENT_DENORMAL
           && (significand & NJS_DBL_HIDDEN_BIT) == 0)
    {
        significand <<= 1;
        exp--;
    }

    if (exp == NJS_DBL_EXPONENT_DENORMAL
        && (significand & NJS_DBL_HIDDEN_BIT) == 0)
    {
        biased_exp = 0;

    } else {
        biased_exp = (uint64_t) (exp + NJS_DBL_EXPONENT_BIAS);
    }

    conv.u64 = (significand & NJS_DBL_SIGNIFICAND_MASK)
                | (biased_exp << NJS_DBL_SIGNIFICAND_SIZE);

    return conv.d;
}


njs_inline njs_diyfp_t
njs_diyfp_shift_left(njs_diyfp_t v, unsigned shift)
{
    return njs_diyfp(v.significand << shift, v.exp - shift);
}


njs_inline njs_diyfp_t
njs_diyfp_shift_right(njs_diyfp_t v, unsigned shift)
{
    return njs_diyfp(v.significand >> shift, v.exp + shift);
}


njs_inline njs_diyfp_t
njs_diyfp_sub(njs_diyfp_t lhs, njs_diyfp_t rhs)
{
    return njs_diyfp(lhs.significand - rhs.significand, lhs.exp);
}


njs_inline njs_diyfp_t
njs_diyfp_mul(njs_diyfp_t lhs, njs_diyfp_t rhs)
{
#if (NJS_HAVE_UNSIGNED_INT128)

    uint64_t       l, h;
    njs_uint128_t  u128;

    u128 = (njs_uint128_t) (lhs.significand)
           * (njs_uint128_t) (rhs.significand);

    h = u128 >> 64;
    l = (uint64_t) u128;

    /* rounding. */

    if (l & ((uint64_t) 1 << 63)) {
        h++;
    }

    return njs_diyfp(h, lhs.exp + rhs.exp + 64);

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

    return njs_diyfp(ac + (ad >> 32) + (bc >> 32) + (tmp >> 32),
                     lhs.exp + rhs.exp + 64);

#endif
}


njs_inline njs_diyfp_t
njs_diyfp_normalize(njs_diyfp_t v)
{
    return njs_diyfp_shift_left(v, njs_leading_zeros64(v.significand));
}


njs_diyfp_t njs_cached_power_dec(int exp, int *dec_exp);
njs_diyfp_t njs_cached_power_bin(int exp, int *dec_exp);


#endif /* _NJS_DIYFP_H_INCLUDED_ */
