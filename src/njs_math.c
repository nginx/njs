
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t
njs_object_math_abs(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, fabs(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_acos(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     num;
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    num = njs_number(&args[1]);

#if (NJS_SOLARIS)
    /* On Solaris acos(x) returns 0 for x > 1. */
    if (fabs(num) > 1.0) {
        num = NAN;
    }
#endif

    njs_set_number(&vm->retval, acos(num));

    return NJS_OK;
}


static njs_int_t
njs_object_math_acosh(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, acosh(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_asin(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     num;
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    num = njs_number(&args[1]);

#if (NJS_SOLARIS)
    /* On Solaris asin(x) returns 0 for x > 1. */
    if (fabs(num) > 1.0) {
        num = NAN;
    }
#endif

    njs_set_number(&vm->retval, asin(num));

    return NJS_OK;
}


static njs_int_t
njs_object_math_asinh(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, asinh(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_atan(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, atan(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_atan2(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     y, x;
    njs_int_t  ret;

    if (njs_slow_path(nargs < 3)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (njs_slow_path(!njs_is_number(&args[2]))) {
        ret = njs_value_to_numeric(vm, &args[2], &args[2]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    y = njs_number(&args[1]);
    x = njs_number(&args[2]);

    njs_set_number(&vm->retval, atan2(y, x));

    return NJS_OK;
}


static njs_int_t
njs_object_math_atanh(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, atanh(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_cbrt(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, cbrt(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_ceil(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, ceil(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_clz32(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t   ui32;
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, 32);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_uint32(vm, &args[1], &ui32);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {
        ui32 = njs_number_to_uint32(njs_number(&args[1]));
    }

    njs_set_number(&vm->retval, njs_leading_zeros(ui32));

    return NJS_OK;
}


static njs_int_t
njs_object_math_cos(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, cos(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_cosh(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, cosh(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_exp(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, exp(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_expm1(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, expm1(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_floor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, floor(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_fround(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, (float) njs_number(&args[1]));

    return NJS_OK;
}


static njs_int_t
njs_object_math_hypot(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double      num;
    njs_int_t   ret;
    njs_uint_t  i;

    for (i = 1; i < nargs; i++) {
        if (!njs_is_numeric(&args[i])) {
            ret = njs_value_to_numeric(vm, &args[i], &args[i]);
            if (ret != NJS_OK) {
                return ret;
            }
        }
    }

    num = (nargs > 1) ? fabs(njs_number(&args[1])) : 0;

    for (i = 2; i < nargs; i++) {
        num = hypot(num, njs_number(&args[i]));

        if (num == INFINITY) {
            break;
        }
    }

    njs_set_number(&vm->retval, num);

    return NJS_OK;
}


static njs_int_t
njs_object_math_imul(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint32_t   a, b;
    njs_int_t  ret;

    if (njs_slow_path(nargs < 3)) {
        njs_set_number(&vm->retval, 0);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_uint32(vm, &args[1], &a);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {
        a = njs_number_to_uint32(njs_number(&args[1]));
    }

    if (njs_slow_path(!njs_is_number(&args[2]))) {
        ret = njs_value_to_uint32(vm, &args[2], &b);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {
        b = njs_number_to_uint32(njs_number(&args[2]));
    }

    njs_set_number(&vm->retval, (int32_t) (a * b));

    return NJS_OK;
}


static njs_int_t
njs_object_math_log(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, log(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_log10(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, log10(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_log1p(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, log1p(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_log2(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     num;
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    num = njs_number(&args[1]);

#if (NJS_SOLARIS)
    /* On Solaris 10 log(-1) returns -Infinity. */
    if (num < 0) {
        num = NAN;
    }
#endif

    njs_set_number(&vm->retval, log2(num));

    return NJS_OK;
}


njs_inline double
njs_fmax(double x, double y)
{
    if (x == 0 && y == 0) {
        return signbit(x) ? y : x;
    }

    return fmax(x, y);
}


njs_inline double
njs_fmin(double x, double y)
{
    if (x == 0 && y == 0) {
        return signbit(x) ? x : y;
    }

    return fmin(x, y);
}


static njs_int_t
njs_object_math_min_max(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t max)
{
    double      num, value;
    njs_int_t   ret;
    njs_uint_t  i;

    value = max ? -INFINITY : INFINITY;

    for (i = 1; i < nargs; i++) {
        ret = njs_value_to_number(vm, &args[i], &num);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_slow_path(isnan(num))) {
            value = num;
            break;
        }

        value = max ? njs_fmax(value, num) : njs_fmin(value, num);
    }

    njs_set_number(&vm->retval, value);

    return NJS_OK;
}


static njs_int_t
njs_object_math_pow(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     num, base, exponent;
    njs_int_t  ret;

    if (njs_slow_path(nargs < 3)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (njs_slow_path(!njs_is_number(&args[2]))) {
        ret = njs_value_to_numeric(vm, &args[2], &args[2]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    base = njs_number(&args[1]);
    exponent = njs_number(&args[2]);

    /*
     * According to ECMA-262:
     *  1. If exponent is NaN, the result should be NaN;
     *  2. The result of Math.pow(+/-1, +/-Infinity) should be NaN.
     */

    if (fabs(base) != 1 || (!isnan(exponent) && !isinf(exponent))) {
        num = pow(base, exponent);

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NJS_OK;
}


static njs_int_t
njs_object_math_random(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    num = njs_random(&vm->random) / 4294967296.0;

    njs_set_number(&vm->retval, num);

    return NJS_OK;
}


static njs_int_t
njs_object_math_round(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint8_t           sign;
    uint64_t          one, fraction_mask;
    njs_int_t         ret, biased_exp;
    njs_diyfp_conv_t  conv;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_numeric(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    conv.d = njs_number(&args[1]);
    biased_exp = (conv.u64 & NJS_DBL_EXPONENT_MASK) >> NJS_DBL_SIGNIFICAND_SIZE;

    if (biased_exp < NJS_DBL_EXPONENT_OFFSET) {

        /* |v| < 1. */

        if (biased_exp == (NJS_DBL_EXPONENT_OFFSET - 1)
            && conv.u64 != njs_uint64(0xbfe00000, 0x00000000))
        {
            /* (|v| > 0.5 || v == 0.5) => +-1.0 */

            conv.u64 = (conv.u64 & NJS_DBL_SIGN_MASK)
                       | (NJS_DBL_EXPONENT_OFFSET << NJS_DBL_SIGNIFICAND_SIZE);

        } else {

            /* (|v| < 0.5 || v == -0.5) => +-0. */

            conv.u64 &= ((uint64_t) 1) << 63;
        }

    } else if (biased_exp < NJS_DBL_EXPONENT_BIAS) {

        /* |v| <= 2^52 - 1 (largest safe integer). */

        one = ((uint64_t) 1) << (NJS_DBL_EXPONENT_BIAS - biased_exp);
        fraction_mask = one - 1;

        /* truncation. */

        sign = conv.u64 >> 63;
        conv.u64 += (one >> 1) - sign;
        conv.u64 &= ~fraction_mask;
    }

    /* |v| >= 2^52, Infinity and NaNs => v. */

    njs_set_number(&vm->retval, conv.d);

    return NJS_OK;
}


static njs_int_t
njs_object_math_sign(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double     num;
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
         njs_set_number(&vm->retval, NAN);
         return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    num = njs_number(&args[1]);

    if (!isnan(num) && num != 0) {
        num = signbit(num) ? -1 : 1;
    }

    njs_set_number(&vm->retval, num);

    return NJS_OK;
}


static njs_int_t
njs_object_math_sin(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, sin(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_sinh(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, sinh(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_sqrt(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, sqrt(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_tan(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, tan(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_tanh(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, tanh(njs_number(&args[1])));

    return NJS_OK;
}


static njs_int_t
njs_object_math_trunc(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    if (njs_slow_path(nargs < 2)) {
        njs_set_number(&vm->retval, NAN);
        return NJS_OK;
    }

    if (njs_slow_path(!njs_is_number(&args[1]))) {
        ret = njs_value_to_numeric(vm, &args[1], &args[1]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_set_number(&vm->retval, trunc(njs_number(&args[1])));

    return NJS_OK;
}


static const njs_object_prop_t  njs_math_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("E"),
        .value = njs_value(NJS_NUMBER, 1, M_E),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("LN10"),
        .value = njs_value(NJS_NUMBER, 1, M_LN10),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("LN2"),
        .value = njs_value(NJS_NUMBER, 1, M_LN2),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("LOG10E"),
        .value = njs_value(NJS_NUMBER, 1, M_LOG10E),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("LOG2E"),
        .value = njs_value(NJS_NUMBER, 1, M_LOG2E),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("PI"),
        .value = njs_value(NJS_NUMBER, 1, M_PI),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("SQRT1_2"),
        .value = njs_value(NJS_NUMBER, 1, M_SQRT1_2),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("SQRT2"),
        .value = njs_value(NJS_NUMBER, 1, M_SQRT2),
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("__proto__"),
        .value = njs_prop_handler(njs_object_prototype_proto),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("abs"),
        .value = njs_native_function(njs_object_math_abs, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("acos"),
        .value = njs_native_function(njs_object_math_acos, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("acosh"),
        .value = njs_native_function(njs_object_math_acosh, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("asin"),
        .value = njs_native_function(njs_object_math_asin, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("asinh"),
        .value = njs_native_function(njs_object_math_asinh, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("atan"),
        .value = njs_native_function(njs_object_math_atan, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("atan2"),
        .value = njs_native_function(njs_object_math_atan2, 2),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("atanh"),
        .value = njs_native_function(njs_object_math_atanh, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("cbrt"),
        .value = njs_native_function(njs_object_math_cbrt, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("ceil"),
        .value = njs_native_function(njs_object_math_ceil, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("clz32"),
        .value = njs_native_function(njs_object_math_clz32, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("cos"),
        .value = njs_native_function(njs_object_math_cos, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("cosh"),
        .value = njs_native_function(njs_object_math_cosh, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("exp"),
        .value = njs_native_function(njs_object_math_exp, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("expm1"),
        .value = njs_native_function(njs_object_math_expm1, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("floor"),
        .value = njs_native_function(njs_object_math_floor, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("fround"),
        .value = njs_native_function(njs_object_math_fround, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("hypot"),
        .value = njs_native_function(njs_object_math_hypot, 2),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("imul"),
        .value = njs_native_function(njs_object_math_imul, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("log"),
        .value = njs_native_function(njs_object_math_log, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("log10"),
        .value = njs_native_function(njs_object_math_log10, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("log1p"),
        .value = njs_native_function(njs_object_math_log1p, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("log2"),
        .value = njs_native_function(njs_object_math_log2, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("max"),
        .value = njs_native_function2(njs_object_math_min_max, 2, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("min"),
        .value = njs_native_function2(njs_object_math_min_max, 2, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("pow"),
        .value = njs_native_function(njs_object_math_pow, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("random"),
        .value = njs_native_function(njs_object_math_random, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("round"),
        .value = njs_native_function(njs_object_math_round, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("sign"),
        .value = njs_native_function(njs_object_math_sign, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sin"),
        .value = njs_native_function(njs_object_math_sin, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("sinh"),
        .value = njs_native_function(njs_object_math_sinh, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sqrt"),
        .value = njs_native_function(njs_object_math_sqrt, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("tan"),
        .value = njs_native_function(njs_object_math_tan, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("tanh"),
        .value = njs_native_function(njs_object_math_tanh, 1),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("trunc"),
        .value = njs_native_function(njs_object_math_trunc, 1),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_math_object_init = {
    njs_math_object_properties,
    njs_nitems(njs_math_object_properties),
};
