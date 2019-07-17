
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <math.h>


static njs_ret_t
njs_object_math_abs(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = fabs(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_acos(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = njs_number(&args[1]);

#if (NXT_SOLARIS)
        /* On Solaris acos(x) returns 0 for x > 1. */
        if (fabs(num) > 1.0) {
            num = NAN;
        }
#endif

        num = acos(num);

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_acosh(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = acosh(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_asin(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = njs_number(&args[1]);

#if (NXT_SOLARIS)
        /* On Solaris asin(x) returns 0 for x > 1. */
        if (fabs(num) > 1.0) {
            num = NAN;
        }
#endif

        num = asin(num);

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_asinh(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = asinh(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_atan(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = atan(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_atan2(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num, y, x;

    if (nargs > 2) {
        y = njs_number(&args[1]);
        x = njs_number(&args[2]);

        num = atan2(y, x);

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_atanh(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = atanh(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_cbrt(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = cbrt(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_ceil(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = ceil(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_clz32(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double    num;
    uint32_t  ui32;

    if (nargs > 1) {
        ui32 = njs_number_to_uint32(njs_number(&args[1]));
        num = nxt_leading_zeros(ui32);

    } else {
        num = 32;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_cos(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = cos(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_cosh(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = cosh(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_exp(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = exp(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_expm1(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = expm1(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_floor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = floor(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_fround(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = (float) njs_number(&args[1]);

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_hypot(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double      num;
    njs_ret_t   ret;
    nxt_uint_t  i;

    for (i = 1; i < nargs; i++) {
        if (!njs_is_numeric(&args[i])) {
            ret = njs_value_to_numeric(vm, &args[i], &args[i]);
            if (ret != NXT_OK) {
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

    return NXT_OK;
}


static njs_ret_t
njs_object_math_imul(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double    num;
    uint32_t  a, b;

    if (nargs > 2) {
        a = njs_number_to_uint32(njs_number(&args[1]));
        b = njs_number_to_uint32(njs_number(&args[2]));

        num = (int32_t) (a * b);

    } else {
        num = 0;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_log(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = log(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_log10(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = log10(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_log1p(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = log1p(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_log2(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = njs_number(&args[1]);

#if (NXT_SOLARIS)
        /* On Solaris 10 log(-1) returns -Infinity. */
        if (num < 0) {
            num = NAN;
        }
#endif

        num = log2(num);

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_max(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double      num;
    njs_ret_t   ret;
    nxt_uint_t  i;

    if (nargs > 1) {
        for (i = 1; i < nargs; i++) {
            if (njs_is_undefined(&args[i])) {
                num = NAN;
                goto done;

            } else if (!njs_is_numeric(&args[i])) {
                ret = njs_value_to_numeric(vm, &args[i], &args[i]);
                if (ret != NXT_OK) {
                    return ret;
                }
            }
        }

        num = njs_number(&args[1]);

        for (i = 2; i < nargs; i++) {
            num = fmax(num, njs_number(&args[i]));
        }

    } else {
        num = -INFINITY;
    }

done:

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_min(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double      num;
    njs_ret_t   ret;
    nxt_uint_t  i;

    if (nargs > 1) {
        for (i = 1; i < nargs; i++) {
            if (!njs_is_numeric(&args[i])) {
                ret = njs_value_to_numeric(vm, &args[i], &args[i]);
                if (ret != NXT_OK) {
                    return ret;
                }
            }
        }

        num = njs_number(&args[1]);

        for (i = 2; i < nargs; i++) {
            num = fmin(num, njs_number(&args[i]));
        }

    } else {
        num = INFINITY;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_pow(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num, base, exponent;

    if (nargs > 2) {
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

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_random(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    num = nxt_random(&vm->random) / 4294967296.0;

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_round(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = round(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_sign(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = njs_number(&args[1]);

        if (!isnan(num) && num != 0) {
            num = signbit(num) ? -1 : 1;
        }

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_sin(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = sin(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_sinh(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = sinh(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_sqrt(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = sqrt(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_tan(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = tan(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_tanh(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = tanh(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_trunc(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = trunc(njs_number(&args[1]));

    } else {
        num = NAN;
    }

    njs_set_number(&vm->retval, num);

    return NXT_OK;
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
        .type = NJS_METHOD,
        .name = njs_string("abs"),
        .value = njs_native_function(njs_object_math_abs,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("acos"),
        .value = njs_native_function(njs_object_math_acos,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("acosh"),
        .value = njs_native_function(njs_object_math_acosh,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("asin"),
        .value = njs_native_function(njs_object_math_asin,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("asinh"),
        .value = njs_native_function(njs_object_math_asinh,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("atan"),
        .value = njs_native_function(njs_object_math_atan,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("atan2"),
        .value = njs_native_function(njs_object_math_atan2,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("atanh"),
        .value = njs_native_function(njs_object_math_atanh,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("cbrt"),
        .value = njs_native_function(njs_object_math_cbrt,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("ceil"),
        .value = njs_native_function(njs_object_math_ceil,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("clz32"),
        .value = njs_native_function(njs_object_math_clz32,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("cos"),
        .value = njs_native_function(njs_object_math_cos,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("cosh"),
        .value = njs_native_function(njs_object_math_cosh,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("exp"),
        .value = njs_native_function(njs_object_math_exp,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("expm1"),
        .value = njs_native_function(njs_object_math_expm1,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("floor"),
        .value = njs_native_function(njs_object_math_floor,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("fround"),
        .value = njs_native_function(njs_object_math_fround,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("hypot"),
        .value = njs_native_function(njs_object_math_hypot, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("imul"),
        .value = njs_native_function(njs_object_math_imul,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("log"),
        .value = njs_native_function(njs_object_math_log,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("log10"),
        .value = njs_native_function(njs_object_math_log10,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("log1p"),
        .value = njs_native_function(njs_object_math_log1p,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("log2"),
        .value = njs_native_function(njs_object_math_log2,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("max"),
        .value = njs_native_function(njs_object_math_max, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("min"),
        .value = njs_native_function(njs_object_math_min, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("pow"),
        .value = njs_native_function(njs_object_math_pow,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("random"),
        .value = njs_native_function(njs_object_math_random, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("round"),
        .value = njs_native_function(njs_object_math_round,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("sign"),
        .value = njs_native_function(njs_object_math_sign,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("sin"),
        .value = njs_native_function(njs_object_math_sin,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("sinh"),
        .value = njs_native_function(njs_object_math_sinh,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("sqrt"),
        .value = njs_native_function(njs_object_math_sqrt,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("tan"),
        .value = njs_native_function(njs_object_math_tan,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("tanh"),
        .value = njs_native_function(njs_object_math_tanh,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("trunc"),
        .value = njs_native_function(njs_object_math_trunc,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_math_object_init = {
    nxt_string("Math"),
    njs_math_object_properties,
    nxt_nitems(njs_math_object_properties),
};
