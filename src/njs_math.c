
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
        .value = njs_string("Math"),
        .configurable = 1,
    },

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
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_ABS),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("acos"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_ACOS),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("acosh"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_ACOSH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("asin"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_ASIN),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("asinh"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_ASINH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("atan"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_ATAN),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("atan2"),
        .value = njs_native_function2(njs_object_math_func, 2, NJS_MATH_ATAN2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("atanh"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_ATANH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("cbrt"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_CBRT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("ceil"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_CEIL),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("clz32"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_CLZ32),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("cos"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_COS),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("cosh"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_COSH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("exp"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_EXP),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("expm1"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_EXPM1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("floor"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_FLOOR),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("fround"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_FROUND),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("hypot"),
        .value = njs_native_function(njs_object_math_hypot, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("imul"),
        .value = njs_native_function2(njs_object_math_func, 2, NJS_MATH_IMUL),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("log"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_LOG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("log10"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_LOG10),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("log1p"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_LOG1P),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("log2"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_LOG2),
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
        .value = njs_native_function2(njs_object_math_func, 2, NJS_MATH_POW),
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
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_ROUND),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sign"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_SIGN),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sin"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_SIN),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sinh"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_SINH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sqrt"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_SQRT),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("tan"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_TAN),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("tanh"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_TANH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("trunc"),
        .value = njs_native_function2(njs_object_math_func, 1, NJS_MATH_TRUNC),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_math_object_init = {
    njs_math_object_properties,
    njs_nitems(njs_math_object_properties),
};
