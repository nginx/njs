
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <nxt_random.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_object.h>
#include <njs_function.h>
#include <math.h>


static njs_ret_t
njs_object_math_abs(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, fabs(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_acos(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, acos(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_asin(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, asin(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_atan(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, atan(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_atan2(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  y, x;

    y = NJS_NAN;
    x = NJS_NAN;

    if (nargs > 1) {
        y = args[1].data.u.number;
    }

    if (nargs > 2) {
        x = args[2].data.u.number;
    }

    njs_number_set(&vm->retval, atan2(y, x));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_ceil(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, ceil(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_cos(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, cos(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_exp(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, exp(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_floor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, floor(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_log(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, log(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_max(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double      num;
    nxt_uint_t  i;

    if (nargs > 1) {
        for (i = 1; i < nargs; i++) {
            if (!njs_is_numeric(&args[i])) {
                vm->frame->trap_scratch.data.u.value = &args[i];
                return NJS_TRAP_NUMBER_ARG;
            }
        }

        num = args[1].data.u.number;

        for (i = 2; i < nargs; i++) {
            num = fmax(num, args[i].data.u.number);
        }

    } else {
        num = -NJS_INFINITY;
    }

    njs_number_set(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_min(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double      num;
    nxt_uint_t  i;

    if (nargs > 1) {
        for (i = 1; i < nargs; i++) {
            if (!njs_is_numeric(&args[i])) {
                vm->frame->trap_scratch.data.u.value = &args[i];
                return NJS_TRAP_NUMBER_ARG;
            }
        }

        num = args[1].data.u.number;

        for (i = 2; i < nargs; i++) {
            num = fmin(num, args[i].data.u.number);
        }

    } else {
        num = NJS_INFINITY;
    }

    njs_number_set(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_pow(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  base, exponent;

    base = NJS_NAN;
    exponent = NJS_NAN;

    if (nargs > 1) {
        base = args[1].data.u.number;
    }

    if (nargs > 2) {
        exponent = args[2].data.u.number;
    }

    njs_number_set(&vm->retval, pow(base, exponent));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_random(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    num = nxt_random(&vm->random) / 4294967296.0;

    njs_number_set(&vm->retval, num);

    return NXT_OK;
}


static njs_ret_t
njs_object_math_round(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, round(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_sin(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, sin(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_sqrt(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, sqrt(num));

    return NXT_OK;
}


static njs_ret_t
njs_object_math_tan(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    if (nargs > 1) {
        num = args[1].data.u.number;

    } else {
        num = NJS_NAN;
    }

    njs_number_set(&vm->retval, tan(num));

    return NXT_OK;
}


static const njs_object_prop_t  njs_math_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("E"),
        .value = njs_value(NJS_NUMBER, 1, 2.718281828459045),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("LN10"),
        .value = njs_value(NJS_NUMBER, 1, 2.302585092994046),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("LN2"),
        .value = njs_value(NJS_NUMBER, 1, 0.6931471805599453),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("LOG10E"),
        .value = njs_value(NJS_NUMBER, 1, 0.4342944819032518),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("LOG2E"),
        .value = njs_value(NJS_NUMBER, 1, 1.4426950408889634),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("PI"),
        .value = njs_value(NJS_NUMBER, 1, 3.141592653589793),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("SQRT1_2"),
        .value = njs_value(NJS_NUMBER, 1, 0.7071067811865476),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("SQRT2"),
        .value = njs_value(NJS_NUMBER, 1, 1.4142135623730951),
    },

    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("__proto__"),
        .value = njs_native_getter(njs_object_prototype_get_proto),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("abs"),
        .value = njs_native_function(njs_object_math_abs, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("acos"),
        .value = njs_native_function(njs_object_math_acos, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("asin"),
        .value = njs_native_function(njs_object_math_asin, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("atan"),
        .value = njs_native_function(njs_object_math_atan, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("atan2"),
        .value = njs_native_function(njs_object_math_atan2, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("ceil"),
        .value = njs_native_function(njs_object_math_ceil, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("cos"),
        .value = njs_native_function(njs_object_math_cos, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("exp"),
        .value = njs_native_function(njs_object_math_exp, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("floor"),
        .value = njs_native_function(njs_object_math_floor, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("log"),
        .value = njs_native_function(njs_object_math_log, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("max"),
        .value = njs_native_function(njs_object_math_max, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("min"),
        .value = njs_native_function(njs_object_math_min, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("pow"),
        .value = njs_native_function(njs_object_math_pow, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("random"),
        .value = njs_native_function(njs_object_math_random, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("round"),
        .value = njs_native_function(njs_object_math_round, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("sin"),
        .value = njs_native_function(njs_object_math_sin, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("sqrt"),
        .value = njs_native_function(njs_object_math_sqrt, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("tan"),
        .value = njs_native_function(njs_object_math_tan, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },
};


const njs_object_init_t  njs_math_object_init = {
    njs_math_object_properties,
    nxt_nitems(njs_math_object_properties),
};
