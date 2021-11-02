
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_VALUE_CONVERSION_H_INCLUDED_
#define _NJS_VALUE_CONVERSION_H_INCLUDED_


njs_inline njs_int_t
njs_value_to_number(njs_vm_t *vm, njs_value_t *value, double *dst)
{
    njs_int_t    ret;
    njs_value_t  primitive;

    if (njs_slow_path(!njs_is_primitive(value))) {
        ret = njs_value_to_primitive(vm, &primitive, value, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        value = &primitive;
    }

    if (njs_slow_path(!njs_is_numeric(value))) {

        if (njs_slow_path(njs_is_symbol(value))) {
            njs_symbol_conversion_failed(vm, 0);
            return NJS_ERROR;
        }

        *dst = NAN;

        if (njs_is_string(value)) {
            *dst = njs_string_to_number(value, 0);
        }

        return NJS_OK;
    }

    *dst = njs_number(value);

    return NJS_OK;
}


njs_inline njs_int_t
njs_value_to_numeric(njs_vm_t *vm, njs_value_t *value, njs_value_t *dst)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, value, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_number(dst, num);

    return NJS_OK;
}


njs_inline njs_int_t
njs_value_to_integer(njs_vm_t *vm, njs_value_t *value, int64_t *dst)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, value, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    *dst = njs_number_to_integer(num);

    return NJS_OK;
}


njs_inline njs_int_t
njs_value_to_length(njs_vm_t *vm, njs_value_t *value, int64_t *dst)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, value, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    *dst = njs_number_to_length(num);

    return NJS_OK;
}


njs_inline njs_int_t
njs_value_to_index(njs_vm_t *vm, njs_value_t *value, uint64_t *dst)
{
    int64_t    integer_index;
    njs_int_t  ret;

    if (njs_slow_path(njs_is_undefined(value))) {
        *dst = 0;

    } else {
        ret = njs_value_to_integer(vm, value, &integer_index);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (integer_index < 0 || integer_index > UINT32_MAX) {
            njs_range_error(vm, "invalid index");
            return NJS_ERROR;
        }

        *dst = integer_index;
    }

    return NJS_OK;
}


njs_inline njs_int_t
njs_value_to_int32(njs_vm_t *vm, njs_value_t *value, int32_t *dst)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, value, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    *dst = njs_number_to_int32(num);

    return NJS_OK;
}


njs_inline njs_int_t
njs_value_to_uint32(njs_vm_t *vm, njs_value_t *value, uint32_t *dst)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, value, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    *dst = njs_number_to_uint32(num);

    return NJS_OK;
}


njs_inline njs_int_t
njs_value_to_uint16(njs_vm_t *vm, njs_value_t *value, uint16_t *dst)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, value, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    *dst = njs_number_to_uint16(num);

    return NJS_OK;
}


njs_inline njs_int_t
njs_value_to_string(njs_vm_t *vm, njs_value_t *dst, njs_value_t *value)
{
    njs_int_t    ret;
    njs_value_t  primitive;

    if (njs_slow_path(!njs_is_primitive(value))) {
        if (njs_slow_path(njs_is_object_symbol(value))) {
            /* should fail */
            value = njs_object_value(value);

        } else {
            ret = njs_value_to_primitive(vm, &primitive, value, 1);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            value = &primitive;
        }
    }

    return njs_primitive_value_to_string(vm, dst, value);
}


/*
 * retval >= 0 is length (UTF8 characters) value of appended string.
 */
njs_inline njs_int_t
njs_value_to_chain(njs_vm_t *vm, njs_chb_t *chain, njs_value_t *value)
{
    njs_int_t    ret;
    njs_value_t  primitive;

    if (njs_slow_path(!njs_is_primitive(value))) {
        if (njs_slow_path(njs_is_object_symbol(value))) {
            /* should fail */
            value = njs_object_value(value);

        } else {
            ret = njs_value_to_primitive(vm, &primitive, value, 1);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            value = &primitive;
        }
    }

    return njs_primitive_value_to_chain(vm, chain, value);
}


#endif /* _NJS_VALUE_CONVERSION_H_INCLUDED_ */
