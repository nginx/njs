
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */


#include <njs_main.h>
#include <stdatomic.h>


typedef enum {
    NJS_ATOMICS_ADD,
    NJS_ATOMICS_SUB,
    NJS_ATOMICS_AND,
    NJS_ATOMICS_OR,
    NJS_ATOMICS_XOR,
    NJS_ATOMICS_EXCHANGE,
    NJS_ATOMICS_COMPARE_EXCHANGE,
    NJS_ATOMICS_LOAD,
} njs_atomics_op_t;


static njs_int_t njs_atomics_get_ptr(njs_vm_t *vm, void **pptr,
    njs_array_buffer_t **pabuf, int *psize_log2, njs_object_type_t *ptype,
    njs_value_t *typedarray, njs_value_t *index_val);
static njs_int_t njs_atomics_op(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic, njs_value_t *retval);
static njs_int_t njs_atomics_store(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_atomics_is_lock_free(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);


static njs_int_t
njs_atomics_get_ptr(njs_vm_t *vm, void **pptr, njs_array_buffer_t **pabuf,
    int *psize_log2, njs_object_type_t *ptype, njs_value_t *typedarray,
    njs_value_t *index_val)
{
    void                *ptr;
    int64_t             index;
    uint32_t            length, element_size;
    njs_int_t           ret;
    njs_object_type_t   type;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    if (!njs_is_typed_array(typedarray)) {
        njs_type_error(vm, "integer TypedArray expected");
        return NJS_ERROR;
    }

    array = njs_typed_array(typedarray);
    type = array->type;

    if (type == NJS_OBJ_TYPE_FLOAT32_ARRAY
        || type == NJS_OBJ_TYPE_FLOAT64_ARRAY)
    {
        njs_type_error(vm, "integer TypedArray expected");
        return NJS_ERROR;
    }

    buffer = array->buffer;

    if (!buffer->shared && njs_is_detached_buffer(buffer)) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    ret = njs_value_to_integer(vm, index_val, &index);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    length = njs_typed_array_length(array);

    if (njs_slow_path(index < 0 || (uint64_t) index >= length)) {
        njs_range_error(vm, "out-of-bound access");
        return NJS_ERROR;
    }

    element_size = njs_typed_array_element_size(type);
    ptr = &array->buffer->u.u8[array->offset * element_size
                                 + index * element_size];

    if (pabuf != NULL) {
        *pabuf = buffer;
    }

    if (psize_log2 != NULL) {
        switch (element_size) {
        case 1:
            *psize_log2 = 0;
            break;
        case 2:
            *psize_log2 = 1;
            break;
        case 4:
            *psize_log2 = 2;
            break;
        case 8:
            *psize_log2 = 3;
            break;
        default:
            return NJS_ERROR;
        }
    }

    if (ptype != NULL) {
        *ptype = type;
    }

    *pptr = ptr;

    return NJS_OK;
}


static njs_int_t
njs_atomics_op(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic, njs_value_t *retval)
{
    void                *ptr;
    int                 size_log2;
    int64_t             value, expected, replacement;
    uint64_t            result;
    njs_int_t           ret;
    njs_atomics_op_t    op;
    njs_object_type_t   type;
    njs_array_buffer_t  *abuf;

    op = magic;
    expected = 0;
    replacement = 0;

    ret = njs_atomics_get_ptr(vm, &ptr, &abuf, &size_log2, &type,
                               njs_arg(args, nargs, 1),
                               njs_arg(args, nargs, 2));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (op != NJS_ATOMICS_LOAD) {
        ret = njs_value_to_integer(vm, njs_arg(args, nargs, 3), &value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (op == NJS_ATOMICS_COMPARE_EXCHANGE) {
            expected = value;
            ret = njs_value_to_integer(vm, njs_arg(args, nargs, 4),
                                       &replacement);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        if (!abuf->shared && njs_is_detached_buffer(abuf)) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

    } else {
        value = 0;
    }

    switch (op | (size_log2 << 3)) {

#define OP(op_name, func_name)                                  \
    case NJS_ATOMICS_ ## op_name | (0 << 3):                    \
        result = func_name((_Atomic(uint8_t) *)ptr, value);     \
        break;                                                  \
    case NJS_ATOMICS_ ## op_name | (1 << 3):                    \
        result = func_name((_Atomic(uint16_t) *)ptr, value);    \
        break;                                                  \
    case NJS_ATOMICS_ ## op_name | (2 << 3):                    \
        result = func_name((_Atomic(uint32_t) *)ptr, value);    \
        break;                                                  \
    case NJS_ATOMICS_ ## op_name | (3 << 3):                    \
        result = func_name((_Atomic(uint64_t) *)ptr, value);    \
        break;

        OP(ADD, atomic_fetch_add)
        OP(SUB, atomic_fetch_sub)
        OP(AND, atomic_fetch_and)
        OP(OR, atomic_fetch_or)
        OP(XOR, atomic_fetch_xor)
        OP(EXCHANGE, atomic_exchange)
#undef OP

    case NJS_ATOMICS_LOAD | (0 << 3):
        result = atomic_load((_Atomic(uint8_t) *)ptr);
        break;
    case NJS_ATOMICS_LOAD | (1 << 3):
        result = atomic_load((_Atomic(uint16_t) *)ptr);
        break;
    case NJS_ATOMICS_LOAD | (2 << 3):
        result = atomic_load((_Atomic(uint32_t) *)ptr);
        break;
    case NJS_ATOMICS_LOAD | (3 << 3):
        result = atomic_load((_Atomic(uint64_t) *)ptr);
        break;

    case NJS_ATOMICS_COMPARE_EXCHANGE | (0 << 3):
        {
            uint8_t exp = expected;
            atomic_compare_exchange_strong((_Atomic(uint8_t) *)ptr, &exp,
                                           replacement);
            result = exp;
        }
        break;
    case NJS_ATOMICS_COMPARE_EXCHANGE | (1 << 3):
        {
            uint16_t exp = expected;
            atomic_compare_exchange_strong((_Atomic(uint16_t) *)ptr, &exp,
                                           replacement);
            result = exp;
        }
        break;
    case NJS_ATOMICS_COMPARE_EXCHANGE | (2 << 3):
        {
            uint32_t exp = expected;
            atomic_compare_exchange_strong((_Atomic(uint32_t) *)ptr, &exp,
                                           replacement);
            result = exp;
        }
        break;
    case NJS_ATOMICS_COMPARE_EXCHANGE | (3 << 3):
        {
            uint64_t exp = expected;
            atomic_compare_exchange_strong((_Atomic(uint64_t) *)ptr, &exp,
                                           replacement);
            result = exp;
        }
        break;

    default:
        njs_internal_error(vm, "invalid atomic operation");
        return NJS_ERROR;
    }

    switch (type) {
    case NJS_OBJ_TYPE_INT8_ARRAY:
        result = (int8_t) result;
        break;

    case NJS_OBJ_TYPE_UINT8_ARRAY:
    case NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY:
        result = (uint8_t) result;
        break;

    case NJS_OBJ_TYPE_INT16_ARRAY:
        result = (int16_t) result;
        break;

    case NJS_OBJ_TYPE_UINT16_ARRAY:
        result = (uint16_t) result;
        break;

    case NJS_OBJ_TYPE_INT32_ARRAY:
        result = (int32_t) result;
        break;

    case NJS_OBJ_TYPE_UINT32_ARRAY:
        result = (uint32_t) result;
        break;

    default:
        break;
    }

    njs_set_number(retval, result);

    return NJS_OK;
}


static njs_int_t
njs_atomics_store(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    void                *ptr;
    int                 size_log2;
    int64_t             value;
    njs_int_t           ret;
    njs_array_buffer_t  *abuf;

    ret = njs_atomics_get_ptr(vm, &ptr, &abuf, &size_log2, NULL,
                               njs_arg(args, nargs, 1),
                               njs_arg(args, nargs, 2));
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 3), &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (!abuf->shared && njs_is_detached_buffer(abuf)) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    switch (size_log2) {
    case 0:
        atomic_store((_Atomic(uint8_t) *)ptr, value);
        break;
    case 1:
        atomic_store((_Atomic(uint16_t) *)ptr, value);
        break;
    case 2:
        atomic_store((_Atomic(uint32_t) *)ptr, value);
        break;
    case 3:
        atomic_store((_Atomic(uint64_t) *)ptr, value);
        break;
    }

    njs_set_number(retval, value);

    return NJS_OK;
}


static njs_int_t
njs_atomics_is_lock_free(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    int64_t     size;
    njs_bool_t  lock_free;
    njs_int_t   ret;

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &size);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    lock_free = (size == 1 || size == 2 || size == 4 || size == 8);

    njs_set_boolean(retval, lock_free);

    return NJS_OK;
}


static const njs_object_prop_init_t  njs_atomics_object_properties[] =
{
    NJS_DECLARE_PROP_VALUE(SYMBOL_toStringTag,
                           njs_ascii_strval("Atomics"),
                           NJS_OBJECT_PROP_VALUE_C),

    NJS_DECLARE_PROP_NATIVE(STRING_add, njs_atomics_op, 3,
                            NJS_ATOMICS_ADD),

    NJS_DECLARE_PROP_NATIVE(STRING_and, njs_atomics_op, 3,
                            NJS_ATOMICS_AND),

    NJS_DECLARE_PROP_NATIVE(STRING_compareExchange, njs_atomics_op, 4,
                            NJS_ATOMICS_COMPARE_EXCHANGE),

    NJS_DECLARE_PROP_NATIVE(STRING_exchange, njs_atomics_op, 3,
                            NJS_ATOMICS_EXCHANGE),

    NJS_DECLARE_PROP_NATIVE(STRING_isLockFree, njs_atomics_is_lock_free, 1,
                            0),

    NJS_DECLARE_PROP_NATIVE(STRING_load, njs_atomics_op, 2,
                            NJS_ATOMICS_LOAD),

    NJS_DECLARE_PROP_NATIVE(STRING_or, njs_atomics_op, 3,
                            NJS_ATOMICS_OR),

    NJS_DECLARE_PROP_NATIVE(STRING_store, njs_atomics_store, 3, 0),

    NJS_DECLARE_PROP_NATIVE(STRING_sub, njs_atomics_op, 3,
                            NJS_ATOMICS_SUB),

    NJS_DECLARE_PROP_NATIVE(STRING_xor, njs_atomics_op, 3,
                            NJS_ATOMICS_XOR),
};


const njs_object_init_t  njs_atomics_object_init = {
    njs_atomics_object_properties,
    njs_nitems(njs_atomics_object_properties),
};
