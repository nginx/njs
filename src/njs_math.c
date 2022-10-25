
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef enum {
    NJS_MATH_ABS,
    NJS_MATH_ACOS,
    NJS_MATH_ACOSH,
    NJS_MATH_ASIN,
    NJS_MATH_ASINH,
    NJS_MATH_ATAN,
    NJS_MATH_ATAN2,
    NJS_MATH_ATANH,
    NJS_MATH_CBRT,
    NJS_MATH_CEIL,
    NJS_MATH_CLZ32,
    NJS_MATH_COS,
    NJS_MATH_COSH,
    NJS_MATH_EXP,
    NJS_MATH_EXPM1,
    NJS_MATH_FLOOR,
    NJS_MATH_FROUND,
    NJS_MATH_IMUL,
    NJS_MATH_LOG,
    NJS_MATH_LOG10,
    NJS_MATH_LOG1P,
    NJS_MATH_LOG2,
    NJS_MATH_POW,
    NJS_MATH_ROUND,
    NJS_MATH_SIGN,
    NJS_MATH_SIN,
    NJS_MATH_SINH,
    NJS_MATH_SQRT,
    NJS_MATH_TAN,
    NJS_MATH_TANH,
    NJS_MATH_TRUNC,
} njs_math_func_t;


static njs_int_t
njs_object_math_func(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
{
    double            num, num2;
    uint8_t           sign;
    uint32_t          u32;
    uint64_t          one, fraction_mask;
    njs_int_t         ret, ep;
    njs_math_func_t   func;
    njs_diyfp_conv_t  conv;

    func = magic;

    ret = njs_value_to_number(vm, njs_arg(args, nargs, 1), &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    switch (func) {
    case NJS_MATH_ABS:
        num = fabs(num);
        break;

    case NJS_MATH_ACOS:
#if (NJS_SOLARIS)
        /* On Solaris acos(x) returns 0 for x > 1. */
        if (fabs(num) > 1.0) {
            num = NAN;
        }
#endif

        num = acos(num);
        break;

    case NJS_MATH_ACOSH:
        num = acosh(num);
        break;

    case NJS_MATH_ASIN:
#if (NJS_SOLARIS)
        /* On Solaris asin(x) returns 0 for x > 1. */
        if (fabs(num) > 1.0) {
            num = NAN;
        }
#endif

        num = asin(num);
        break;

    case NJS_MATH_ASINH:
        num = asinh(num);
        break;

    case NJS_MATH_ATAN:
        num = atan(num);
        break;

    case NJS_MATH_ATANH:
        num = atanh(num);
        break;

    case NJS_MATH_CBRT:
        num = cbrt(num);
        break;

    case NJS_MATH_CEIL:
        num = ceil(num);
        break;

    case NJS_MATH_CLZ32:
        u32 = njs_number_to_uint32(num);
        num = njs_leading_zeros(u32);
        break;

    case NJS_MATH_COS:
        num = cos(num);
        break;

    case NJS_MATH_COSH:
        num = cosh(num);
        break;

    case NJS_MATH_EXP:
        num = exp(num);
        break;

    case NJS_MATH_EXPM1:
        num = expm1(num);
        break;

    case NJS_MATH_FLOOR:
        num = floor(num);
        break;

    case NJS_MATH_FROUND:
        num = (float) num;
        break;

    case NJS_MATH_LOG:
        num = log(num);
        break;

    case NJS_MATH_LOG10:
        num = log10(num);
        break;

    case NJS_MATH_LOG1P:
        num = log1p(num);
        break;

    case NJS_MATH_LOG2:
#if (NJS_SOLARIS)
        /* On Solaris 10 log(-1) returns -Infinity. */
        if (num < 0) {
            num = NAN;
        }
#endif

        num = log2(num);
        break;

    case NJS_MATH_SIGN:
        if (!isnan(num) && num != 0) {
            num = signbit(num) ? -1 : 1;
        }

        break;

    case NJS_MATH_SIN:
        num = sin(num);
        break;

    case NJS_MATH_SINH:
        num = sinh(num);
        break;

    case NJS_MATH_SQRT:
        num = sqrt(num);
        break;

    case NJS_MATH_ROUND:
        conv.d = num;
        ep = (conv.u64 & NJS_DBL_EXPONENT_MASK) >> NJS_DBL_SIGNIFICAND_SIZE;

        if (ep < NJS_DBL_EXPONENT_OFFSET) {

            /* |v| < 1. */

            if (ep == (NJS_DBL_EXPONENT_OFFSET - 1)
                && conv.u64 != njs_uint64(0xbfe00000, 0x00000000))
            {
                /* (|v| > 0.5 || v == 0.5) => +-1.0 */

                conv.u64 = conv.u64 & NJS_DBL_SIGN_MASK;
                conv.u64 |= NJS_DBL_EXPONENT_OFFSET << NJS_DBL_SIGNIFICAND_SIZE;

            } else {

                /* (|v| < 0.5 || v == -0.5) => +-0. */

                conv.u64 &= ((uint64_t) 1) << 63;
            }

        } else if (ep < NJS_DBL_EXPONENT_BIAS) {

            /* |v| <= 2^52 - 1 (largest safe integer). */

            one = ((uint64_t) 1) << (NJS_DBL_EXPONENT_BIAS - ep);
            fraction_mask = one - 1;

            /* truncation. */

            sign = conv.u64 >> 63;
            conv.u64 += (one >> 1) - sign;
            conv.u64 &= ~fraction_mask;
        }

        num = conv.d;
        break;

    case NJS_MATH_TAN:
        num = tan(num);
        break;

    case NJS_MATH_TANH:
        num = tanh(num);
        break;

    case NJS_MATH_TRUNC:
        num = trunc(num);
        break;

    default:
        ret = njs_value_to_number(vm, njs_arg(args, nargs, 2), &num2);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        switch (func) {
        case NJS_MATH_ATAN2:
            num = atan2(num, num2);
            break;

        case NJS_MATH_IMUL:
            u32 = njs_number_to_uint32(num);
            num = (int32_t) (u32 * njs_number_to_uint32(num2));
            break;

        default:
            /*
             * According to ECMA-262:
             *  1. If exponent is NaN, the result should be NaN;
             *  2. The result of Math.pow(+/-1, +/-Infinity) should be NaN.
             */

            if (fabs(num) != 1 || (!isnan(num2) && !isinf(num2))) {
                num = pow(num, num2);

            } else {
                num = NAN;
            }
        }
    }

    njs_set_number(&vm->retval, num);

    return NJS_OK;
}


static njs_int_t
njs_object_math_hypot(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double      num;
    njs_int_t   ret;
    njs_uint_t  i;

    ret = njs_value_to_number(vm, njs_arg(args, nargs, 1), &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    num = (nargs > 1) ? fabs(num) : 0;

    for (i = 2; i < nargs; i++) {
        ret = njs_value_to_numeric(vm, &args[i], &args[i]);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        num = hypot(num, njs_number(&args[i]));

        if (njs_slow_path(isinf(num))) {
            break;
        }
    }

    njs_set_number(&vm->retval, num);

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
njs_object_math_random(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    num = njs_random(&vm->random) / 4294967296.0;

    njs_set_number(&vm->retval, num);

    return NJS_OK;
}


static const njs_object_prop_t  njs_math_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .u.value = njs_string("Math"),
        .configurable = 1,
    },

    NJS_DECLARE_PROP_HANDLER("prototype", njs_object_prototype_create, 0, 0, 0),

    NJS_DECLARE_PROP_VALUE("E", njs_value(NJS_NUMBER, 1, M_E), 0),

    NJS_DECLARE_PROP_VALUE("LN10", njs_value(NJS_NUMBER, 1, M_LN10), 0),

    NJS_DECLARE_PROP_VALUE("LN2", njs_value(NJS_NUMBER, 1, M_LN2), 0),

    NJS_DECLARE_PROP_VALUE("LOG10E", njs_value(NJS_NUMBER, 1, M_LOG10E), 0),

    NJS_DECLARE_PROP_VALUE("LOG2E", njs_value(NJS_NUMBER, 1, M_LOG2E), 0),

    NJS_DECLARE_PROP_VALUE("PI", njs_value(NJS_NUMBER, 1, M_PI), 0),

    NJS_DECLARE_PROP_VALUE("SQRT1_2", njs_value(NJS_NUMBER, 1, M_SQRT1_2), 0),

    NJS_DECLARE_PROP_VALUE("SQRT2", njs_value(NJS_NUMBER, 1, M_SQRT2), 0),

    NJS_DECLARE_PROP_NATIVE("abs", njs_object_math_func, 1, NJS_MATH_ABS),

    NJS_DECLARE_PROP_NATIVE("acos", njs_object_math_func, 1, NJS_MATH_ACOS),

    NJS_DECLARE_PROP_NATIVE("acosh", njs_object_math_func, 1, NJS_MATH_ACOSH),

    NJS_DECLARE_PROP_NATIVE("asin", njs_object_math_func, 1, NJS_MATH_ASIN),

    NJS_DECLARE_PROP_NATIVE("asinh", njs_object_math_func, 1, NJS_MATH_ASINH),

    NJS_DECLARE_PROP_NATIVE("atan", njs_object_math_func, 1, NJS_MATH_ATAN),

    NJS_DECLARE_PROP_NATIVE("atan2", njs_object_math_func, 2, NJS_MATH_ATAN2),

    NJS_DECLARE_PROP_NATIVE("atanh", njs_object_math_func, 1, NJS_MATH_ATANH),

    NJS_DECLARE_PROP_NATIVE("cbrt", njs_object_math_func, 1, NJS_MATH_CBRT),

    NJS_DECLARE_PROP_NATIVE("ceil", njs_object_math_func, 1, NJS_MATH_CEIL),

    NJS_DECLARE_PROP_NATIVE("clz32", njs_object_math_func, 1, NJS_MATH_CLZ32),

    NJS_DECLARE_PROP_NATIVE("cos", njs_object_math_func, 1, NJS_MATH_COS),

    NJS_DECLARE_PROP_NATIVE("cosh", njs_object_math_func, 1, NJS_MATH_COSH),

    NJS_DECLARE_PROP_NATIVE("exp", njs_object_math_func, 1, NJS_MATH_EXP),

    NJS_DECLARE_PROP_NATIVE("expm1", njs_object_math_func, 1, NJS_MATH_EXPM1),

    NJS_DECLARE_PROP_NATIVE("floor", njs_object_math_func, 1, NJS_MATH_FLOOR),

    NJS_DECLARE_PROP_NATIVE("fround", njs_object_math_func, 1,
                            NJS_MATH_FROUND),

    NJS_DECLARE_PROP_NATIVE("hypot", njs_object_math_hypot, 2, 0),

    NJS_DECLARE_PROP_NATIVE("imul", njs_object_math_func, 2, NJS_MATH_IMUL),

    NJS_DECLARE_PROP_NATIVE("log", njs_object_math_func, 1, NJS_MATH_LOG),

    NJS_DECLARE_PROP_NATIVE("log10", njs_object_math_func, 1, NJS_MATH_LOG10),

    NJS_DECLARE_PROP_NATIVE("log1p", njs_object_math_func, 1, NJS_MATH_LOG1P),

    NJS_DECLARE_PROP_NATIVE("log2", njs_object_math_func, 1, NJS_MATH_LOG2),

    NJS_DECLARE_PROP_NATIVE("max", njs_object_math_min_max, 2, 1),

    NJS_DECLARE_PROP_NATIVE("min", njs_object_math_min_max, 2, 0),

    NJS_DECLARE_PROP_NATIVE("pow", njs_object_math_func, 2, NJS_MATH_POW),

    NJS_DECLARE_PROP_NATIVE("random", njs_object_math_random, 0, 0),

    NJS_DECLARE_PROP_NATIVE("round", njs_object_math_func, 1, NJS_MATH_ROUND),

    NJS_DECLARE_PROP_NATIVE("sign", njs_object_math_func, 1, NJS_MATH_SIGN),

    NJS_DECLARE_PROP_NATIVE("sin", njs_object_math_func, 1, NJS_MATH_SIN),

    NJS_DECLARE_PROP_NATIVE("sinh", njs_object_math_func, 1, NJS_MATH_SINH),

    NJS_DECLARE_PROP_NATIVE("sqrt", njs_object_math_func, 1, NJS_MATH_SQRT),

    NJS_DECLARE_PROP_NATIVE("tan", njs_object_math_func, 1, NJS_MATH_TAN),

    NJS_DECLARE_PROP_NATIVE("tanh", njs_object_math_func, 1, NJS_MATH_TANH),

    NJS_DECLARE_PROP_NATIVE("trunc", njs_object_math_func, 1, NJS_MATH_TRUNC),
};


const njs_object_init_t  njs_math_object_init = {
    njs_math_object_properties,
    njs_nitems(njs_math_object_properties),
};
