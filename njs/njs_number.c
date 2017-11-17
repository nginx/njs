
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_string.h>
#include <njs_object.h>
#include <njs_array.h>
#include <njs_function.h>
#include <njs_error.h>
#include <string.h>
#include <stdio.h>
#include <float.h>


/*
 * 2^53 - 1 is the largest integer n such that n and n + 1
 * as well as -n and -n - 1 are all exactly representable
 * in the IEEE-754 format.
 */
#define NJS_MAX_SAFE_INTEGER  ((1LL << 53) - 1)


static njs_ret_t njs_number_to_string_radix(njs_vm_t *vm, njs_value_t *string,
    double number, uint32_t radix);


uint32_t
njs_value_to_index(njs_value_t *value)
{
    double       num;
    njs_array_t  *array;

    num = NAN;

    if (nxt_fast_path(njs_is_numeric(value))) {
        num = value->data.u.number;

    } else if (njs_is_string(value)) {
        num = njs_string_to_index(value);

    } else if (njs_is_array(value)) {

        array = value->data.u.array;

        if (nxt_lvlhsh_is_empty(&array->object.hash)) {

            if (array->length == 0) {
                /* An empty array value is zero. */
                return 0;
            }

            if (array->length == 1 && njs_is_valid(&array->start[0])) {
                /* A single value array is the zeroth array value. */
                return njs_value_to_index(&array->start[0]);
            }
        }
    }

    if ((uint32_t) num == num) {
        return (uint32_t) num;
    }

    return NJS_ARRAY_INVALID_INDEX;
}


double
njs_number_dec_parse(u_char **start, u_char *end)
{
    u_char      c, *e, *p;
    double      num, frac, scale, exponent;
    nxt_bool_t  minus;

    p = *start;

    num = 0;

    while (p < end) {
        /* Values less than '0' become >= 208. */
        c = *p - '0';

        if (nxt_slow_path(c > 9)) {
            break;
        }

        num = num * 10 + c;
        p++;
    }

    if (p < end && *p == '.') {

        frac = 0;
        scale = 1;

        for (p++; p < end; p++) {
            /* Values less than '0' become >= 208. */
            c = *p - '0';

            if (nxt_slow_path(c > 9)) {
                break;
            }

            frac = frac * 10 + c;
            scale *= 10;
        }

        num += frac / scale;
    }

    e = p + 1;

    if (e < end && (*p == 'e' || *p == 'E')) {
        minus = 0;

        if (e + 1 < end) {
            if (*e == '-') {
                e++;
                minus = 1;

            } else if (*e == '+') {
                e++;
            }
        }

        /* Values less than '0' become >= 208. */
        c = *e - '0';

        if (nxt_fast_path(c <= 9)) {
            exponent = c;
            p = e + 1;

            while (p < end) {
                /* Values less than '0' become >= 208. */
                c = *p - '0';

                if (nxt_slow_path(c > 9)) {
                    break;
                }

                exponent = exponent * 10 + c;
                p++;
            }

            if (num != 0) {
                exponent = minus ? -exponent : exponent;
                num = num * pow(10.0, exponent);
            }
        }
    }

    *start = p;

    return num;
}


uint64_t
njs_number_oct_parse(u_char **start, u_char *end)
{
    u_char    c, *p;
    uint64_t  num;

    p = *start;

    num = 0;

    while (p < end) {
        /* Values less than '0' become >= 208. */
        c = *p - '0';

        if (nxt_slow_path(c > 7)) {
            break;
        }

        num = num * 8 + c;
        p++;
    }

    *start = p;

    return num;
}


uint64_t
njs_number_hex_parse(u_char **start, u_char *end)
{
    u_char    c, *p;
    uint64_t  num;

    p = *start;

    num = 0;

    while (p < end) {
        c = (u_char) (*p | 0x20);

        /* Values less than '0' become >= 208. */
        c = c - '0';

        if (c > 9) {
            /* Values less than 'a' become >= 159. */
            c = c - ('a' - '0');

            if (nxt_slow_path(c > 5)) {
                break;
            }

            c += 10;
        }

        num = num * 16 + c;
        p++;
    }

    *start = p;

    return num;
}


int64_t
njs_number_radix_parse(u_char **start, u_char *end, uint8_t radix)
{
    u_char    *p;
    uint8_t   d;
    int64_t   num;
    uint64_t  n;

    static const int8_t  digits[256]
        nxt_aligned(32) =
    {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    num = -1;
    n = 0;

    for (p = *start; p < end; p++) {
        d = digits[*p];

        if (nxt_slow_path(d >= radix)) {
            break;
        }

        n = (n * radix) + d;
        num = n;
    }

    *start = p;

    return num;
}


njs_ret_t
njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number)
{
    double             num;
    size_t             size;
    const njs_value_t  *value;
    u_char             buf[128];

    num = number->data.u.number;

    if (isnan(num)) {
        value = &njs_string_nan;

    } else if (isinf(num)) {

        if (num < 0) {
            value = &njs_string_minus_infinity;

        } else {
            value = &njs_string_plus_infinity;
        }

    } else {
        size = njs_num_to_buf(num, buf, sizeof(buf));

        return njs_string_new(vm, string, buf, size, size);
    }

    *string = *value;

    return NXT_OK;
}


size_t
njs_num_to_buf(double num, u_char *buf, size_t size)
{
    double      n;
    const char  *fmt;

    n = fabs(num);

    if (n == 0) {
        fmt = "%g";

    } else if (n < 1) {
        fmt = "%f";

    } else if (n < 1000000) {
        fmt = "%g";

    } else if (n < 1e20) {
        fmt = "%1.f";

    } else {
        fmt = "%1.e";
    }

    return snprintf((char *) buf, size, fmt, num);
}


njs_ret_t
njs_number_constructor(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_object_t       *object;
    const njs_value_t  *value;

    if (nargs == 1) {
        value = &njs_value_zero;

    } else {
        value = &args[1];
    }

    if (vm->top_frame->ctor) {
        object = njs_object_value_alloc(vm, value, value->type);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

        vm->retval.data.u.object = object;
        vm->retval.type = NJS_OBJECT_NUMBER;
        vm->retval.data.truth = 1;

    } else {
        vm->retval = *value;
    }

    return NXT_OK;
}


static njs_ret_t
njs_number_is_integer(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    const njs_value_t  *value;

    value = &njs_value_false;

    if (nargs > 1 && njs_is_number(&args[1])) {
        num = args[1].data.u.number;

        if (num == trunc(num) && !isinf(num)) {
            value = &njs_value_true;
        }
    }

    vm->retval = *value;

    return NXT_OK;
}



static njs_ret_t
njs_number_is_safe_integer(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    const njs_value_t  *value;

    value = &njs_value_false;

    if (nargs > 1 && njs_is_number(&args[1])) {
        num = args[1].data.u.number;

        if (num == (int64_t) num && fabs(num) <= NJS_MAX_SAFE_INTEGER) {
            value = &njs_value_true;
        }
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_number_is_nan(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = &njs_value_false;

    if (nargs > 1
        && njs_is_number(&args[1])
        && isnan(args[1].data.u.number))
    {
        value = &njs_value_true;
    }

    vm->retval = *value;

    return NXT_OK;
}


static const njs_object_prop_t  njs_number_constructor_properties[] =
{
    /* Number.name == "Number". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Number"),
    },

    /* Number.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
    },

    /* Number.prototype. */
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("prototype"),
        .value = njs_native_getter(njs_object_prototype_create),
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("EPSILON"),
        .value = njs_value(NJS_NUMBER, 1, DBL_EPSILON),
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("MAX_SAFE_INTEGER"),
        .value = njs_value(NJS_NUMBER, 1, NJS_MAX_SAFE_INTEGER),
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("MIN_SAFE_INTEGER"),
        .value = njs_value(NJS_NUMBER, 1, -NJS_MAX_SAFE_INTEGER),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("MAX_VALUE"),
        .value = njs_value(NJS_NUMBER, 1, DBL_MAX),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("MIN_VALUE"),
        .value = njs_value(NJS_NUMBER, 1, DBL_MIN),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("NaN"),
        .value = njs_value(NJS_NUMBER, 0, NAN),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("POSITIVE_INFINITY"),
        .value = njs_value(NJS_NUMBER, 1, INFINITY),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("NEGATIVE_INFINITY"),
        .value = njs_value(NJS_NUMBER, 1, -INFINITY),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("isFinite"),
        .value = njs_native_function(njs_number_is_finite, 0, 0),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("isInteger"),
        .value = njs_native_function(njs_number_is_integer, 0, 0),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("isSafeInteger"),
        .value = njs_native_function(njs_number_is_safe_integer, 0, 0),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("isNaN"),
        .value = njs_native_function(njs_number_is_nan, 0, 0),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("parseFloat"),
        .value = njs_native_function(njs_number_parse_float, 0,
                     NJS_SKIP_ARG, NJS_STRING_ARG),
    },

    /* ES6. */
    {
        .type = NJS_METHOD,
        .name = njs_string("parseInt"),
        .value = njs_native_function(njs_number_parse_int, 0,
                     NJS_SKIP_ARG, NJS_STRING_ARG, NJS_INTEGER_ARG),
    },
};


const njs_object_init_t  njs_number_constructor_init = {
    nxt_string("Number"),
    njs_number_constructor_properties,
    nxt_nitems(njs_number_constructor_properties),
};


static njs_ret_t
njs_number_prototype_value_of(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_NUMBER) {

        if (value->type == NJS_OBJECT_NUMBER) {
            value = &value->data.u.object_value->value;

        } else {
            njs_exception_type_error(vm, NULL, NULL);
            return NXT_ERROR;
        }
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_number_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    double       number, radix;
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_NUMBER) {

        if (value->type == NJS_OBJECT_NUMBER) {
            value = &value->data.u.object_value->value;

        } else {
            njs_exception_type_error(vm, NULL, NULL);
            return NXT_ERROR;
        }
    }

    if (nargs > 1) {
        radix = args[1].data.u.number;

        if (radix < 2 || radix > 36 || radix != (int) radix) {
            njs_exception_range_error(vm, NULL, NULL);
            return NXT_ERROR;
        }

        number = value->data.u.number;

        if (radix != 10 && !isnan(number) && !isinf(number)) {
            return njs_number_to_string_radix(vm, &vm->retval, number, radix);
        }
    }

    return njs_number_to_string(vm, &vm->retval, value);
}


/*
 * The radix equal to 2 produces the longest intergral value of a number
 * and the maximum value consists of 1024 digits and minus sign.
 */

#define NJS_STRING_RADIX_INTERGRAL_LEN  (1 + 1024)
#define NJS_STRING_RADIX_FRACTION_LEN   (1 + 54)
#define NJS_STRING_RADIX_LEN                                                  \
    (NJS_STRING_RADIX_INTERGRAL_LEN + NJS_STRING_RADIX_FRACTION_LEN)


static njs_ret_t
njs_number_to_string_radix(njs_vm_t *vm, njs_value_t *string,
    double number, uint32_t radix)
{
    u_char   *p, *f, *end;
    double   n, next;
    size_t   size;
    uint8_t  reminder;
    u_char   buf[NJS_STRING_RADIX_LEN];

    static const char  *digits = "0123456789abcdefghijklmnopqrstuvwxyz";

    end = buf + NJS_STRING_RADIX_LEN;
    p = buf + NJS_STRING_RADIX_INTERGRAL_LEN;

    n = number;

    if (n < 0) {
        n = -n;
    }

    do {
        next = trunc(n / radix);
        reminder = n - next * radix;
        *(--p) = digits[reminder];
        n = next;
    } while (n != 0);

    n = number;

    if (n < 0) {
        *(--p) = '-';
    }

    f = buf + NJS_STRING_RADIX_INTERGRAL_LEN;

    n = n - trunc(n);

    if (n != 0) {
        *f++ = '.';

        do {
            n = n * radix;
            reminder = trunc(n);
            *f++ = digits[reminder];
            n = n - reminder;
        } while (n != 0 && f < end);
    }

    size = f - p;

    return njs_string_new(vm, string, p, size, size);
}


static const njs_object_prop_t  njs_number_prototype_properties[] =
{
    {
        .type = NJS_NATIVE_GETTER,
        .name = njs_string("__proto__"),
        .value = njs_native_getter(njs_primitive_prototype_get_proto),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_number_prototype_value_of, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_number_prototype_to_string, 0,
                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
    },
};


const njs_object_init_t  njs_number_prototype_init = {
    nxt_string("Number"),
    njs_number_prototype_properties,
    nxt_nitems(njs_number_prototype_properties),
};


njs_ret_t
njs_number_global_is_nan(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = &njs_value_true;

    if (nargs > 1 && !isnan(args[1].data.u.number)) {
        value = &njs_value_false;
    }

    vm->retval = *value;

    return NXT_OK;
}


njs_ret_t
njs_number_is_finite(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    const njs_value_t  *value;

    value = &njs_value_false;

    if (nargs > 1 && njs_is_number(&args[1])) {
        num = args[1].data.u.number;

        if (!isnan(num) && !isinf(num)) {
            value = &njs_value_true;
        }
    }

    vm->retval = *value;

    return NXT_OK;
}


njs_ret_t
njs_number_parse_int(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    u_char             *p, *end;
    int64_t            n;
    uint8_t            radix;
    nxt_str_t          string;
    nxt_bool_t         minus, test_prefix;

    num = NAN;

    if (nargs > 1) {
        njs_string_get(&args[1], &string);

        end = string.start + string.length;

        for (p = string.start; p < end; p++) {
            if (*p != ' ') {
                goto found;
            }
        }

        goto done;

    found:

        minus = 0;

        if (p[0] == '-') {
            p++;
            minus = 1;

        } else if (p[0] == '+') {
            p++;
        }

        test_prefix = (end - p > 1);
        radix = 0;

        if (nargs > 2) {
            radix = args[2].data.u.number;

            if (radix != 0) {
                if (radix < 2 || radix > 36) {
                    goto done;
                }

                if (radix != 16) {
                    test_prefix = 0;
                }
            }
        }

        if (radix == 0) {
            radix = 10;
        }

        if (test_prefix && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            radix = 16;
        }

        n = njs_number_radix_parse(&p, end, radix);

        if (n >= 0) {
            num = minus ? -n : n;
        }
    }

done:

    njs_number_set(&vm->retval, num);

    return NXT_OK;
}


njs_ret_t
njs_number_parse_float(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    num = NAN;

    if (nargs > 1) {
        num = njs_string_to_number(&args[1], 1);
    }

    njs_number_set(&vm->retval, num);

    return NXT_OK;
}


nxt_noinline uint32_t
njs_number_to_integer(double num)
{
    int64_t  i64;

    /*
     * ES5.1: integer must be modulo 2^32.
     * 2^53 is the largest integer number which can be stored safely
     * in the IEEE-754 format and numbers less than 2^53 can be just
     * converted to int64_t eliding more expensive fmod() operation.
     * Then the int64 integer is truncated to uint32_t.  The NaN is
     * converted to 0x8000000000000000 and becomes 0 after truncation.
     * fmod() of the Infinity returns NaN.
     */

    if (fabs(num) > 9007199254740992.0) {
        i64 = fmod(num, 4294967296.0);

    } else {
        i64 = num;
    }

    return (uint32_t) i64;
}
