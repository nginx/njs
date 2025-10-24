
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


/*
 * 2^53 - 1 is the largest integer n such that n and n + 1
 * as well as -n and -n - 1 are all exactly representable
 * in the IEEE-754 format.
 */
#define NJS_MAX_SAFE_INTEGER  ((1LL << 53) - 1)


static njs_int_t njs_number_to_string_radix(njs_vm_t *vm, njs_value_t *string,
    double number, uint32_t radix);


double
njs_key_to_index(const njs_value_t *value)
{
    njs_array_t  *array;

    if (njs_fast_path(njs_is_numeric(value))) {
        return njs_number(value);

    } else if (njs_is_string(value)) {
        return njs_string_to_index(value);

    } else if (njs_is_array(value)) {

        array = njs_array(value);

        if (njs_flathsh_is_empty(&array->object.hash)) {

            if (array->length == 0) {
                /* An empty array value is zero. */
                return 0;
            }

            if (array->length == 1 && njs_is_valid(&array->start[0])) {
                /* A single value array is the zeroth array value. */
                return njs_key_to_index(&array->start[0]);
            }
        }
    }

    return NAN;
}


double
njs_number_hex_parse(const u_char **start, const u_char *end,
    njs_bool_t literal)
{
    double        num;
    njs_int_t     n;
    const u_char  *p, *_;

    p = *start;

    num = 0;
    _ = p - 1;

    for (; p < end; p++) {
        n = njs_char_to_hex(*p);

        if (njs_slow_path(n < 0)) {
            if (literal && *p == '_' && (p - _) > 1) {
                _ = p;
                continue;
            }

            break;
        }

        num = num * 16 + n;
    }

    *start = p;

    return num;
}


njs_int_t
njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number)
{
    double  num;
    size_t  size;
    u_char  buf[128];

    num = njs_number(number);

    if (isnan(num)) {
        njs_atom_to_value(vm, string, NJS_ATOM_STRING_NaN);

    } else if (isinf(num)) {

        if (num < 0) {
            njs_atom_to_value(vm, string, NJS_ATOM_STRING__Infinity);

        } else {
            njs_atom_to_value(vm, string, NJS_ATOM_STRING_Infinity);
        }

    } else {
        size = njs_dtoa(num, (char *) buf);

        return njs_string_new(vm, string, buf, size, size);
    }

    return NJS_OK;
}


njs_int_t
njs_int64_to_string(njs_vm_t *vm, njs_value_t *value, int64_t i64)
{
    size_t  size;
    u_char  buf[128];

    size = njs_dtoa(i64, (char *) buf);

    return njs_string_new(vm, value, buf, size, size);
}


njs_int_t
njs_number_to_chain(njs_vm_t *vm, njs_chb_t *chain, double num)
{
    size_t  size;
    u_char  *p;

    if (isnan(num)) {
        njs_chb_append_literal(chain, "NaN");
        return njs_length("NaN");

    }

    if (isinf(num)) {
        if (num < 0) {
            njs_chb_append_literal(chain, "-Infinity");
            return njs_length("-Infinity");

        } else {
            njs_chb_append_literal(chain, "Infinity");
            return njs_length("Infinity");
        }
    }

    p = njs_chb_reserve(chain, 64);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    size = njs_dtoa(num, (char *) p);

    njs_chb_written(chain, size);

    return size;
}


static njs_int_t
njs_number_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t           ret;
    njs_value_t         *value;
    njs_object_value_t  *object;

    if (nargs == 1) {
        value = njs_value_arg(&njs_value_zero);

    } else {
        value = &args[1];

        if (njs_slow_path(!njs_is_number(value))) {
            ret = njs_value_to_numeric(vm, value, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }
        }
    }

    if (vm->top_frame->ctor) {
        object = njs_object_value_alloc(vm, NJS_OBJ_TYPE_NUMBER, 0, value);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        njs_set_object_value(retval, object);

    } else {
        njs_set_number(retval, njs_number(value));
    }

    return NJS_OK;
}


static njs_int_t
njs_number_is_integer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    double      num;
    njs_bool_t  integer;

    integer = 0;

    if (nargs > 1 && njs_is_number(&args[1])) {
        num = njs_number(&args[1]);

        if (num == trunc(num) && !isinf(num)) {
            integer = 1;
        }
    }

    njs_set_boolean(retval, integer);

    return NJS_OK;
}



static njs_int_t
njs_number_is_safe_integer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    double      num;
    njs_bool_t  integer;

    integer = 0;

    if (nargs > 1 && njs_is_number(&args[1])) {
        num = njs_number(&args[1]);

        if (num == njs_unsafe_cast_double_to_int64(num)
            && fabs(num) <= NJS_MAX_SAFE_INTEGER)
        {
            integer = 1;
        }
    }

    njs_set_boolean(retval, integer);

    return NJS_OK;
}


static njs_int_t
njs_number_is_nan(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_bool_t  nan;

    nan = 0;

    if (nargs > 1
        && njs_is_number(&args[1])
        && isnan(njs_number(&args[1])))
    {
        nan = 1;
    }

    njs_set_boolean(retval, nan);

    return NJS_OK;
}


static njs_int_t
njs_number_is_finite(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    double      num;
    njs_bool_t  finite;

    finite = 0;

    if (nargs > 1 && njs_is_number(&args[1])) {
        num = njs_number(&args[1]);

        if (!isnan(num) && !isinf(num)) {
            finite = 1;
        }
    }

    njs_set_boolean(retval, finite);

    return NJS_OK;
}


static const njs_object_prop_init_t  njs_number_constructor_properties[] =
{
    NJS_DECLARE_PROP_LENGTH(1),

    NJS_DECLARE_PROP_NAME("Number"),

    NJS_DECLARE_PROP_HANDLER(STRING_prototype, njs_object_prototype_create,
                             0, 0),

    NJS_DECLARE_PROP_VALUE(STRING_EPSILON,
                           njs_value(NJS_NUMBER, 1, DBL_EPSILON), 0),

    NJS_DECLARE_PROP_VALUE(STRING_MAX_SAFE_INTEGER,
                           njs_value(NJS_NUMBER, 1, NJS_MAX_SAFE_INTEGER), 0),

    NJS_DECLARE_PROP_VALUE(STRING_MIN_SAFE_INTEGER,
                           njs_value(NJS_NUMBER, 1, -NJS_MAX_SAFE_INTEGER), 0),

    NJS_DECLARE_PROP_VALUE(STRING_MAX_VALUE,
                           njs_value(NJS_NUMBER, 1, DBL_MAX), 0),

    NJS_DECLARE_PROP_VALUE(STRING_MIN_VALUE,
                           njs_value(NJS_NUMBER, 1, DBL_MIN), 0),

    NJS_DECLARE_PROP_VALUE(STRING_NaN, njs_value(NJS_NUMBER, 0, NAN), 0),

    NJS_DECLARE_PROP_VALUE(STRING_POSITIVE_INFINITY,
                           njs_value(NJS_NUMBER, 1, INFINITY), 0),

    NJS_DECLARE_PROP_VALUE(STRING_NEGATIVE_INFINITY,
                           njs_value(NJS_NUMBER, 1, -INFINITY), 0),

    NJS_DECLARE_PROP_NATIVE(STRING_isFinite, njs_number_is_finite, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_isInteger, njs_number_is_integer, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_isSafeInteger,
                            njs_number_is_safe_integer, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_isNaN, njs_number_is_nan, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_parseFloat, njs_number_parse_float, 1,
                            0),

    NJS_DECLARE_PROP_NATIVE(STRING_parseInt, njs_number_parse_int, 2, 0),
};


static const njs_object_init_t  njs_number_constructor_init = {
    njs_number_constructor_properties,
    njs_nitems(njs_number_constructor_properties),
};


static njs_int_t
njs_number_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_value_t  *value;

    value = njs_argument(args, 0);

    if (value->type != NJS_NUMBER) {

        if (njs_is_object_number(value)) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    njs_value_assign(retval, value);

    return NJS_OK;
}


static njs_int_t
njs_number_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    double       number;
    int32_t      radix;
    njs_int_t    ret;
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_NUMBER) {

        if (njs_is_object_number(value)) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    if (nargs > 1) {
        ret = njs_value_to_int32(vm, &args[1], &radix);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (radix < 2 || radix > 36 || radix != (int) radix) {
            njs_range_error(vm, "radix argument must be between 2 and 36");
            return NJS_ERROR;
        }

        number = njs_number(value);

        if (radix != 10 && !isnan(number) && !isinf(number) && number != 0) {
            return njs_number_to_string_radix(vm, retval, number, radix);
        }
    }

    return njs_number_to_string(vm, retval, value);
}


static njs_int_t
njs_number_prototype_to_fixed(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    double         number;
    size_t         size;
    int64_t        frac;
    njs_int_t      ret;
    njs_value_t    *value;
    JSDTOATempMem  tmp_mem;
    u_char         buf[128];

    /* 128 > 100 + 21 + njs_length(".-\0"). */

    value = &args[0];

    if (value->type != NJS_NUMBER) {
        if (njs_is_object_number(value)) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &frac);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(frac < 0 || frac > 100)) {
        njs_range_error(vm, "digits argument must be between 0 and 100");
        return NJS_ERROR;
    }

    number = njs_number(value);

    if (njs_slow_path(isnan(number) || fabs(number) >= 1e21)) {
        return njs_number_to_string(vm, retval, value);
    }

    size = njs_dtoa2((char *) buf, number, 10, (int) frac,
                     JS_DTOA_FORMAT_FRAC, &tmp_mem);

    return njs_string_new(vm, retval, buf, size, size);
}


static njs_int_t
njs_number_prototype_to_precision(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    double         number;
    size_t         size;
    int64_t        precision;
    njs_int_t      ret;
    njs_value_t    *value;
    JSDTOATempMem  tmp_mem;
    u_char         buf[128];

    /* 128 > 100 + 21 + njs_length(".-\0"). */

    value = &args[0];

    if (value->type != NJS_NUMBER) {
        if (njs_is_object_number(value)) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    if (njs_is_undefined(njs_arg(args, nargs, 1))) {
        return njs_number_to_string(vm, retval, value);
    }

    ret = njs_value_to_integer(vm, njs_argument(args, 1), &precision);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    number = njs_number(value);

    if (njs_slow_path(isnan(number) || isinf(number))) {
        return njs_number_to_string(vm, retval, value);
    }

    if (njs_slow_path(precision < 1 || precision > 100)) {
        njs_range_error(vm, "precision argument must be between 1 and 100");
        return NJS_ERROR;
    }

    size = njs_dtoa2((char *) buf, number, 10, (int) precision,
                     JS_DTOA_FORMAT_FIXED, &tmp_mem);

    return njs_string_new(vm, retval, buf, size, size);
}


static njs_int_t
njs_number_prototype_to_exponential(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    int            digits, flags;
    double         number;
    size_t         size;
    int64_t        frac;
    njs_int_t      ret;
    njs_value_t    *value, *value_frac;
    JSDTOATempMem  tmp_mem;
    u_char         buf[128];

    value = &args[0];

    if (value->type != NJS_NUMBER) {
        if (njs_is_object_number(value)) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    value_frac = njs_arg(args, nargs, 1);

    ret = njs_value_to_integer(vm, value_frac, &frac);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    number = njs_number(value);

    if (njs_slow_path(isnan(number) || isinf(number))) {
        return njs_number_to_string(vm, retval, value);
    }

    if (njs_is_defined(value_frac)) {
        if (njs_slow_path(frac < 0 || frac > 100)) {
            njs_range_error(vm, "digits argument must be between 0 and 100");
            return NJS_ERROR;
        }

    } else {
        frac = -1;
    }

    if (frac < 0) {
        digits = 0;
        flags = JS_DTOA_FORMAT_FREE;

    } else {
        digits = frac + 1;
        flags = JS_DTOA_FORMAT_FIXED;
    }

    size = njs_dtoa2((char *) buf, number, 10, digits,
                     flags | JS_DTOA_EXP_ENABLED, &tmp_mem);

    return njs_string_new(vm, retval, buf, size, size);
}


/*
 * njs_dtoa_max_len() caps radix conversions (format free, no exponent) at 1088
 * characters plus the terminating null, so a fixed 1.1KB stack buffer is safe.
 */
#define NJS_NUMBER_RADIX_BUF_SIZE  1100

static njs_int_t
njs_number_to_string_radix(njs_vm_t *vm, njs_value_t *string,
    double number, uint32_t radix)
{
    int            len;
    size_t         size;
    u_char         buf[NJS_NUMBER_RADIX_BUF_SIZE];
    njs_int_t      ret;
    JSDTOATempMem  tmp_mem;

    len = njs_dtoa_max_len(number, (int) radix, 0,
                           JS_DTOA_FORMAT_FREE | JS_DTOA_EXP_DISABLED);
    if (njs_slow_path((size_t) len + 1 > NJS_NUMBER_RADIX_BUF_SIZE)) {
        njs_internal_error(vm, "radix buffer overflow");
        return NJS_ERROR;
    }

    size = njs_dtoa2((char *) buf, number, (int) radix, 0,
                     JS_DTOA_FORMAT_FREE | JS_DTOA_EXP_DISABLED, &tmp_mem);

    ret = njs_string_new(vm, string, buf, (uint32_t) size, (uint32_t) size);

    return ret;
}


static const njs_object_prop_init_t  njs_number_prototype_properties[] =
{
    NJS_DECLARE_PROP_HANDLER(STRING___proto__,
                             njs_primitive_prototype_get_proto, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_HANDLER(STRING_constructor,
                             njs_object_prototype_create_constructor, 0,
                             NJS_OBJECT_PROP_VALUE_CW),

    NJS_DECLARE_PROP_NATIVE(STRING_valueOf, njs_number_prototype_value_of,
                            0, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_toString,
                            njs_number_prototype_to_string, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_toFixed, njs_number_prototype_to_fixed,
                            1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_toPrecision,
                            njs_number_prototype_to_precision, 1, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_toExponential,
                            njs_number_prototype_to_exponential, 1, 0),
};


const njs_object_init_t  njs_number_prototype_init = {
    njs_number_prototype_properties,
    njs_nitems(njs_number_prototype_properties),
};


njs_int_t
njs_number_global_is_nan(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, njs_arg(args, nargs, 1), &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_boolean(retval, isnan(num));

    return NJS_OK;
}


njs_int_t
njs_number_global_is_finite(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, njs_arg(args, nargs, 1), &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_boolean(retval, !(isnan(num) || isinf(num)));

    return NJS_OK;
}


njs_int_t
njs_number_parse_int(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    double             num;
    int32_t            radix;
    njs_int_t          ret;
    njs_value_t        *value, lvalue;
    njs_string_prop_t  string;

    num = NAN;
    radix = 0;
    value = njs_lvalue_arg(&lvalue, args, nargs, 1);

    ret = njs_value_to_string(vm, value, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (nargs > 2) {
        ret = njs_value_to_int32(vm, njs_argument(args, 2), &radix);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (radix != 0) {
            if (radix < 2 || radix > 36) {
                goto done;
            }
        }
    }

    (void) njs_string_trim(vm, value, &string, NJS_TRIM_START);

    num = njs_atod((char *) string.start, NULL, radix,
                   JS_ATOD_INT_ONLY | JS_ATOD_ACCEPT_PREFIX_AFTER_SIGN);

done:

    njs_set_number(retval, num);

    return NJS_OK;
}


njs_int_t
njs_number_parse_float(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    double             num;
    njs_int_t          ret;
    njs_value_t        *value, lvalue;
    njs_string_prop_t  string;

    value = njs_lvalue_arg(&lvalue, args, nargs, 1);

    ret = njs_value_to_string(vm, value, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    (void) njs_string_trim(vm, value, &string, NJS_TRIM_START);

    num = njs_atod((char *) string.start, NULL, 10, 0);

    njs_set_number(retval, num);

    return NJS_OK;
}


const njs_object_type_init_t  njs_number_type_init = {
   .constructor = njs_native_ctor(njs_number_constructor, 1, 0),
   .constructor_props = &njs_number_constructor_init,
   .prototype_props = &njs_number_prototype_init,
   .prototype_value = { .object_value = {
                            .value = njs_value(NJS_NUMBER, 0, 0.0),
                            .object = { .type = NJS_OBJECT_VALUE } }
                      },
};
