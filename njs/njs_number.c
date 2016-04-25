
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
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
#include <string.h>
#include <stdio.h>


static njs_ret_t njs_number_to_string_radix(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number, uint32_t radix);


double
njs_value_to_number(njs_value_t *value)
{
    njs_array_t  *array;

    if (nxt_fast_path(njs_is_numeric(value))) {
        return value->data.u.number;
    }

    if (njs_is_string(value)) {
        return njs_string_to_number(value, 1);
    }

    if (njs_is_array(value)) {

        array = value->data.u.array;

        if (nxt_lvlhsh_is_empty(&array->object.hash)) {

            if (array->length == 0) {
                /* An empty array value is zero. */
                return 0.0;
            }

            if (array->length == 1 && njs_is_valid(&array->start[0])) {
                /* A single value array is the zeroth array value. */
                return njs_value_to_number(&array->start[0]);
            }
        }
    }

    return NJS_NAN;
}


double
njs_number_parse(const u_char **start, const u_char *end)
{
    u_char        c;
    double        num, frac, scale;
    const u_char  *p;

    /* TODO: "1e2" */

    p = *start;
    c = *p++;

    /* Values below '0' become >= 208. */
    c = c - '0';

    num = c;

    while (p < end) {
        c = *p;

        /* Values below '0' become >= 208. */
        c = c - '0';

        if (nxt_slow_path(c > 9)) {
            break;
        }

        num = num * 10 + c;
        p++;
    }

    if (*p == '.') {

        frac = 0;
        scale = 1;

        for (p++; p < end; p++) {
            c = *p;

            /* Values below '0' become >= 208. */
            c = c - '0';

            if (nxt_slow_path(c > 9)) {
                break;
            }

            frac = frac * 10 + c;
            scale *= 10;
        }

        num += frac / scale;
    }

    *start = p;

    return num;
}


int64_t
njs_number_radix_parse(u_char *p, u_char *end, uint8_t radix, nxt_bool_t exact)
{
    uint8_t   d;
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

    n = 0;

    while (p < end) {
        d = digits[*p++];

        if (nxt_slow_path(d >= radix)) {
            return (exact) ? -1 : (int64_t) n;
        }

        n = (n * radix) + d;
    }

    return n;
}


njs_ret_t
njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number)
{
    double             n, num;
    size_t             size;
    const char         *fmt;
    const njs_value_t  *value;
    u_char             buf[128];

    num = number->data.u.number;

    if (njs_is_nan(num)) {
        value = &njs_string_nan;

    } else if (njs_is_infinity(num)) {

        if (num < 0) {
            value = &njs_string_minus_infinity;

        } else {
            value = &njs_string_plus_infinity;
        }

    } else {
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

        size = snprintf((char *) buf, sizeof(buf), fmt, num);

        return njs_string_new(vm, string, buf, size, size);
    }

    *string = *value;

    return NXT_OK;
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

    if (vm->frame->ctor) {
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
};


const njs_object_init_t  njs_number_constructor_init = {
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
            vm->exception = &njs_exception_type_error;
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
    double       radix;
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_NUMBER) {

        if (value->type == NJS_OBJECT_NUMBER) {
            value = &value->data.u.object_value->value;

        } else {
            vm->exception = &njs_exception_type_error;
            return NXT_ERROR;
        }
    }

    if (nargs == 1 || args[1].data.u.number == 10) {
        return njs_number_to_string(vm, &vm->retval, value);
    }

    radix = args[1].data.u.number;

    if (radix < 2 || radix > 36 || radix != (int) radix) {
        vm->exception = &njs_exception_range_error;
        return NXT_ERROR;
    }

    return njs_number_to_string_radix(vm, &vm->retval, value, radix);
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
    const njs_value_t *number, uint32_t radix)
{
    u_char   *p, *f, *end;
    double   n, next;
    size_t   size;
    uint8_t  reminder;
    u_char   buf[NJS_STRING_RADIX_LEN];

    static const char  *digits = "0123456789abcdefghijklmnopqrstuvwxyz";

    end = buf + NJS_STRING_RADIX_LEN;
    p = buf + NJS_STRING_RADIX_INTERGRAL_LEN;

    n = number->data.u.number;

    if (n < 0) {
        n = -n;
    }

    do {
        next = trunc(n / radix);
        reminder = n - next * radix;
        *(--p) = digits[reminder];
        n = next;
    } while (n != 0);

    n = number->data.u.number;

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
    njs_number_prototype_properties,
    nxt_nitems(njs_number_prototype_properties),
};


njs_ret_t
njs_number_is_nan(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = &njs_value_true;

    if (nargs > 1 && !njs_is_nan(args[1].data.u.number)) {
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

    if (nargs > 1) {
        num = args[1].data.u.number;

        if (!njs_is_nan(num) && !njs_is_infinity(num)) {
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
    nxt_bool_t         minus;
    njs_string_prop_t  string;

    num = NJS_NAN;

    if (nargs > 1) {
        (void) njs_string_prop(&string, &args[1]);

        p = string.start;
        end = string.start + string.size;

        while (p < end) {
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

        if (end - p > 1 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            radix = 16;

        } else {
            radix = 10;
        }

        if (nargs > 2) {
            n = args[2].data.u.number;

            if (n != 0) {

                if (n < 2 || n > 36) {
                    goto done;
                }

                radix = n;
            }
        }

        n = njs_number_radix_parse(p, end, radix, 0);

        if (n >= 0) {
            num = (minus) ? -n : n;
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

    num = NJS_NAN;

    if (nargs > 1) {
        num = njs_string_to_number(&args[1], 0);
    }

    njs_number_set(&vm->retval, num);

    return NXT_OK;
}
