
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t
njs_typed_array_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
{
    double              num;
    uint32_t            element_size;
    uint64_t            i, length, size, offset;
    njs_int_t           ret;
    njs_value_t         *value, prop;
    njs_array_t         *src_array;
    njs_object_type_t   type;
    njs_typed_array_t   *array, *src_tarray;
    njs_array_buffer_t  *buffer;

    size = 0;
    length = 0;
    offset = 0;

    buffer = NULL;
    src_array = NULL;
    src_tarray = NULL;

    type = magic;
    element_size = njs_typed_array_element_size(type);

    if (!vm->top_frame->ctor) {
        njs_type_error(vm, "Constructor of TypedArray requires 'new'");
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 1);

    if (njs_is_array_buffer(value)) {
        buffer = njs_array_buffer(value);

        ret = njs_value_to_index(vm, njs_arg(args, nargs, 2), &offset);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        if (njs_slow_path((offset % element_size) != 0)) {
            njs_range_error(vm, "start offset must be multiple of %uD",
                            element_size);
            return NJS_ERROR;
        }

        if (!njs_is_undefined(njs_arg(args, nargs, 3))) {
            ret = njs_value_to_index(vm, njs_argument(args, 3), &size);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }

            size *= element_size;

            if (njs_slow_path((offset + size) > buffer->size)) {
                njs_range_error(vm, "Invalid typed array length: %uL", size);
                return NJS_ERROR;
            }

        } else {
            if (njs_slow_path((buffer->size % element_size) != 0)) {
                njs_range_error(vm, "byteLength of buffer must be "
                                "multiple of %uD", element_size);
                return NJS_ERROR;
            }

            if (offset > buffer->size) {
                njs_range_error(vm, "byteOffset %uL is outside the bound of "
                                "the buffer", offset);
                return NJS_ERROR;
            }

            size = buffer->size - offset;
        }

    } else if (njs_is_typed_array(value)) {
        src_tarray = njs_typed_array(value);
        size = (uint64_t) njs_typed_array_length(src_tarray) * element_size;

    } else if (njs_is_object(value)) {
        if (njs_is_array(value) && njs_object_hash_is_empty(value)) {
            src_array = njs_array(value);
            length = src_array->length;

        } else {
            ret = njs_object_length(vm, value, &length);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }
        }

        size = length * element_size;

    } else {
        ret = njs_value_to_index(vm, value, &size);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        size *= element_size;
    }

    if (buffer == NULL) {
        buffer = njs_array_buffer_alloc(vm, size);
        if (njs_slow_path(buffer == NULL)) {
            return NJS_ERROR;
        }
    }

    array = njs_mp_zalloc(vm->mem_pool, sizeof(njs_typed_array_t));
    if (njs_slow_path(array == NULL)) {
        goto memory_error;
    }

    array->buffer = buffer;
    array->offset = offset / element_size;
    array->byte_length = size;
    array->type = type;

    if (src_tarray != NULL) {
        if (type != src_tarray->type) {
            length = njs_typed_array_length(src_tarray);
            for (i = 0; i < length; i++) {
                njs_typed_array_set(array, i,
                                    njs_typed_array_get(src_tarray, i));
            }

        } else {
            memcpy(&buffer->u.u8[0], &src_tarray->buffer->u.u8[0], size);
        }

    } else if (src_array != NULL) {
        for (i = 0; i < length; i++) {
            ret = njs_value_to_number(vm, &src_array->start[i], &num);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }

            if (ret == NJS_OK) {
                njs_typed_array_set(array, i, num);
            }
        }

    } else if (!njs_is_array_buffer(value) && njs_is_object(value)) {
        for (i = 0; i < length; i++) {
            ret = njs_value_property_i64(vm, value, i, &prop);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }

            num = NAN;

            if (ret == NJS_OK) {
                ret = njs_value_to_number(vm, &prop, &num);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return NJS_ERROR;
                }
            }

            njs_typed_array_set(array, i, num);
        }
    }

    njs_lvlhsh_init(&array->object.hash);
    njs_lvlhsh_init(&array->object.shared_hash);
    array->object.__proto__ = &vm->prototypes[type].object;
    array->object.type = NJS_TYPED_ARRAY;
    array->object.extensible = 1;
    array->object.fast_array = 1;

    njs_set_typed_array(&vm->retval, array);

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_typed_array_get_this(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    vm->retval = args[0];

    return NJS_OK;
}


static const njs_value_t  njs_typed_array_uint8_tag = njs_string("Uint8Array");
static const njs_value_t  njs_typed_array_uint8_clamped_tag =
                                        njs_long_string("Uint8ClampedArray");
static const njs_value_t  njs_typed_array_int8_tag = njs_string("Int8Array");
static const njs_value_t  njs_typed_array_uint16_tag =
                                                    njs_string("Uint16Array");
static const njs_value_t  njs_typed_array_int16_tag = njs_string("Int16Array");
static const njs_value_t  njs_typed_array_uint32_tag =
                                                    njs_string("Uint32Array");
static const njs_value_t  njs_typed_array_int32_tag = njs_string("Int32Array");
static const njs_value_t  njs_typed_array_float32_tag =
                                                    njs_string("Float32Array");
static const njs_value_t  njs_typed_array_float64_tag =
                                                    njs_string("Float64Array");

static njs_int_t
njs_typed_array_get_string_tag(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t  *this;

    static const njs_value_t  *tags[NJS_OBJ_TYPE_TYPED_ARRAY_SIZE] = {
        &njs_typed_array_uint8_tag,
        &njs_typed_array_uint8_clamped_tag,
        &njs_typed_array_int8_tag,
        &njs_typed_array_uint16_tag,
        &njs_typed_array_int16_tag,
        &njs_typed_array_uint32_tag,
        &njs_typed_array_int32_tag,
        &njs_typed_array_float32_tag,
        &njs_typed_array_float64_tag,
    };

    this = njs_argument(args, 0);

    if (!njs_is_typed_array(this)) {
        njs_set_undefined(&vm->retval);
        return NJS_OK;
    }

    vm->retval = *tags[njs_typed_array_index(njs_typed_array(this)->type)];

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_length(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t        *this;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (!njs_is_typed_array(this)) {
        njs_type_error(vm, "Method TypedArray.prototype.length called "
                       "on incompatible receiver");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);

    njs_set_number(&vm->retval, njs_typed_array_length(array));

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_buffer(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t        *this;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (!njs_is_typed_array(this)) {
        njs_type_error(vm, "Method TypedArray.prototype.buffer called "
                       "on incompatible receiver");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);

    njs_set_array_buffer(&vm->retval, njs_typed_array_buffer(array));

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_byte_length(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t        *this;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (!njs_is_typed_array(this)) {
        njs_type_error(vm, "Method TypedArray.prototype.byteLength called "
                       "on incompatible receiver");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);

    njs_set_number(&vm->retval, array->byte_length);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_byte_offset(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t        *this;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (!njs_is_typed_array(this)) {
        njs_type_error(vm, "Method TypedArray.prototype.byteOffset called "
                       "on incompatible receiver");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);

    njs_set_number(&vm->retval, array->offset
                                * njs_typed_array_element_size(array->type));

    return NJS_OK;
}


njs_int_t
njs_typed_array_set_value(njs_vm_t *vm, njs_typed_array_t *array,
    uint32_t index, njs_value_t *setval)
{
    double     num;
    njs_int_t  ret;

    ret = njs_value_to_number(vm, setval, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_typed_array_set(array, index, num);

    njs_set_number(setval, num);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_set(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double             num;
    uint32_t           i;
    int64_t            length, src_length, offset;
    njs_int_t          ret;
    njs_value_t        *this, *src, *value, prop;
    njs_array_t        *array;
    njs_typed_array_t  *self, *src_tarray;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    self = njs_typed_array(this);
    src = njs_arg(args, nargs, 1);
    value = njs_arg(args, nargs, 2);

    ret = njs_value_to_integer(vm, value, &offset);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(offset < 0)) {
        njs_range_error(vm, "offset is out of bounds");
        return NJS_ERROR;
    }

    length = njs_typed_array_length(self);

    if (njs_is_typed_array(src)) {
        src_tarray = njs_typed_array(src);
        src_length = njs_typed_array_length(src_tarray);

        if (njs_slow_path((src_length > length)
                          || (offset > length - src_length)))
        {
            njs_range_error(vm, "source is too large");
            return NJS_ERROR;
        }

        length = njs_min(njs_typed_array_length(src_tarray), length - offset);

        for (i = 0; i < length; i++) {
            njs_typed_array_set(self, offset + i,
                                njs_typed_array_get(src_tarray, i));
        }

    } else {
        if (njs_is_array(src) && njs_object_hash_is_empty(src)) {
            array = njs_array(src);
            src_length = array->length;

            if (njs_slow_path((src_length > length)
                              || (offset > length - src_length)))
            {
                njs_range_error(vm, "source is too large");
                return NJS_ERROR;
            }

            length = njs_min(array->length, length - offset);

            for (i = 0; i < length; i++) {
                ret = njs_value_to_number(vm, &array->start[i], &num);
                if (ret == NJS_OK) {
                    njs_typed_array_set(self, offset + i, num);
                }
            }

            goto done;
        }

        ret = njs_value_to_object(vm, src);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_object_length(vm, src, (uint64_t *) &src_length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        if (njs_slow_path((src_length > length)
                          || (offset > length - src_length)))
        {
            njs_range_error(vm, "source is too large");
            return NJS_ERROR;
        }

        length = njs_min(src_length, length - offset);

        for (i = 0; i < length; i++) {
            ret = njs_value_property_i64(vm, src, i, &prop);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NJS_ERROR;
            }

            num = NAN;

            if (ret == NJS_OK) {
                ret = njs_value_to_number(vm, &prop, &num);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return NJS_ERROR;
                }
            }

            njs_typed_array_set(self, offset + i, num);
        }
    }

done:

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_fill(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    float               f32;
    int8_t              i8;
    double              num;
    int16_t             i16;
    int32_t             i32;
    uint8_t             u8;
    int64_t             start, end, offset;
    uint32_t            i, length;
    njs_int_t           ret;
    njs_value_t         *this, *setval, lvalue;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    length = njs_typed_array_length(array);

    setval = njs_lvalue_arg(&lvalue, args, nargs, 1);
    ret = njs_value_to_number(vm, setval, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &start);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    start = (start < 0) ? njs_max(length + start, 0) : njs_min(start, length);

    if (njs_is_undefined(njs_arg(args, nargs, 3))) {
        end = length;

    } else {
        ret = njs_value_to_integer(vm, njs_arg(args, nargs, 3), &end);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    end = (end < 0) ? njs_max(length + end, 0) : njs_min(end, length);

    njs_set_typed_array(&vm->retval, array);

    buffer = array->buffer;
    offset = array->offset;

    switch (array->type) {
    case NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY:
        if (isnan(num) || num < 0) {
            u8 = 0;

        } else if (num > 255) {
            u8 = 255;

        } else {
            u8 = lrint(num);
        }

        if (start < end) {
            memset(&buffer->u.u8[start + offset], u8, end - start);
        }

        break;

    case NJS_OBJ_TYPE_UINT8_ARRAY:
    case NJS_OBJ_TYPE_INT8_ARRAY:
        i8 = njs_number_to_int32(num);

        if (start < end) {
            memset(&buffer->u.u8[start + offset], i8, end - start);
        }

        break;

    case NJS_OBJ_TYPE_UINT16_ARRAY:
    case NJS_OBJ_TYPE_INT16_ARRAY:
        i16 = njs_number_to_int32(num);

        for (i = start; i < end; i++) {
            buffer->u.u16[i + offset] = i16;
        }

        break;

    case NJS_OBJ_TYPE_UINT32_ARRAY:
    case NJS_OBJ_TYPE_INT32_ARRAY:
        i32 = njs_number_to_int32(num);

        for (i = start; i < end; i++) {
            buffer->u.u32[i + offset] = i32;
        }

        break;

    case NJS_OBJ_TYPE_FLOAT32_ARRAY:
        f32 = num;

        for (i = start; i < end; i++) {
            buffer->u.f32[i + offset] = f32;
        }

        break;

    default:

        /* NJS_OBJ_TYPE_FLOAT64_ARRAY. */

        for (i = start; i < end; i++) {
            buffer->u.f64[i + offset] = num;
        }
	}

    return NJS_OK;
}


static njs_int_t
njs_typed_array_species_create(njs_vm_t *vm, njs_value_t *exemplar,
    njs_value_t *args, njs_uint_t nargs, njs_value_t *retval)
{
    njs_int_t          ret;
    njs_value_t        this, constructor;
    njs_object_t       *object;
    njs_typed_array_t  *array;

    array = njs_typed_array(exemplar);

    njs_set_function(&constructor, &vm->constructors[array->type]);

    ret = njs_value_species_constructor(vm, exemplar, &constructor,
                                        &constructor);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    object = njs_function_new_object(vm, &constructor);
    if (njs_slow_path(object == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&this, object);

    ret = njs_function_call2(vm, njs_function(&constructor), &this,
                             args, nargs, retval, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (!njs_is_typed_array(retval)) {
        njs_type_error(vm, "Derived TypedArray constructor "
                       "returned not a typed array");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_slice(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t copy)
{
    int64_t             start, end, count, offset;
    uint32_t            i, element_size, length;
    njs_int_t           ret;
    njs_value_t         arguments[3], *this, *value;
    njs_typed_array_t   *array, *new_array;
    njs_array_buffer_t  *buffer, *new_buffer;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    length = njs_typed_array_length(array);
    buffer = njs_typed_array_buffer(array);

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &start);
    if (njs_slow_path(ret != NJS_OK)) {
        njs_range_error(vm, "invalid start");
        return NJS_ERROR;
    }

    start = (start < 0) ? njs_max(length + start, 0) : njs_min(start, length);

    value = njs_arg(args, nargs, 2);

    if (njs_is_undefined(value)) {
        end = length;

    } else {
        ret = njs_value_to_integer(vm, value, &end);
        if (njs_slow_path(ret != NJS_OK)) {
            njs_range_error(vm, "invalid end");
            return NJS_ERROR;
        }
    }

    end = (end < 0) ? njs_max(length + end, 0) : njs_min(end, length);

    element_size = njs_typed_array_element_size(array->type);

    if (copy) {
        count = njs_max(end - start, 0);
        njs_set_number(&arguments[0], count);

        ret = njs_typed_array_species_create(vm, this,
                                             njs_value_arg(arguments), 1,
                                             &vm->retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        new_array = njs_typed_array(&vm->retval);
        if (njs_typed_array_length(new_array) < count) {
            njs_type_error(vm, "Derived TypedArray constructor is "
                           "too small array");
            return NJS_ERROR;
        }

        new_buffer = njs_typed_array_buffer(new_array);
        element_size = njs_typed_array_element_size(array->type);

        if (njs_fast_path(array->type == new_array->type)) {
            start = start * element_size;
            count = count * element_size;

            memcpy(&new_buffer->u.u8[0], &buffer->u.u8[start], count);

        } else {
            for (i = 0; i < count; i++) {
                njs_typed_array_set(new_array, i,
                                    njs_typed_array_get(array, i + start));
            }
        }

        return NJS_OK;
    }

    offset = array->offset * element_size;
    offset += start * element_size;

    njs_set_array_buffer(&arguments[0], buffer);
    njs_set_number(&arguments[1], offset);
    njs_set_number(&arguments[2], njs_max(end - start, 0));

    return njs_typed_array_species_create(vm, this, njs_value_arg(arguments), 3,
                                          &vm->retval);
}


static njs_int_t
njs_typed_array_prototype_copy_within(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    int64_t             length, to, from, final, count;
    uint32_t            element_size;
    njs_int_t           ret;
    njs_value_t         *this, *value;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    length = njs_typed_array_length(array);

    value = njs_arg(args, nargs, 1);

    ret = njs_value_to_integer(vm, value, &to);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    to = to < 0 ? njs_max(to + length, 0) : njs_min(to, length);

    value = njs_arg(args, nargs, 2);

    ret = njs_value_to_integer(vm, value, &from);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    from = from < 0 ? njs_max(from + length, 0) : njs_min(from, length);

    value = njs_arg(args, nargs, 3);

    if (njs_is_undefined(value)) {
        final = length;

    } else {
        ret = njs_value_to_integer(vm, value, &final);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    final = (final < 0) ? njs_max(final + length, 0) : njs_min(final, length);

    njs_set_typed_array(&vm->retval, array);

    count = njs_min(final - from, length - to);

    if (count <= 0) {
        return NJS_OK;
    }

    buffer = njs_typed_array_buffer(array);
    element_size = njs_typed_array_element_size(array->type);

    to = to * element_size;
    from = from * element_size;
    count = count * element_size;

    memmove(&buffer->u.u8[to], &buffer->u.u8[from], count);

    return NJS_OK;
}


njs_int_t
njs_typed_array_to_chain(njs_vm_t *vm, njs_chb_t *chain,
    njs_typed_array_t *array, njs_value_t *sep)
{
    size_t             size, length, arr_length;
    uint32_t           i;
    njs_string_prop_t  separator;

    if (sep == NULL) {
        sep = njs_value_arg(&njs_string_comma);
    }

    (void) njs_string_prop(&separator, sep);

    arr_length = njs_typed_array_length(array);

    if (arr_length == 0) {
        return 0;
    }

    for (i = 0; i < arr_length; i++) {
        njs_number_to_chain(vm, chain, njs_typed_array_get(array, i));
        njs_chb_append(chain, separator.start, separator.size);
    }

    njs_chb_drop(chain, separator.size);

    size = njs_chb_size(chain);

    if (njs_utf8_length(separator.start, separator.size) >= 0) {
        length = size - (separator.size - separator.length) * (arr_length - 1);

    } else {
        length = 0;
    }

    return length;
}


static njs_int_t
njs_typed_array_prototype_join(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    u_char             *p;
    size_t             size, length, arr_length;
    njs_int_t          ret;
    njs_chb_t          chain;
    njs_value_t        *this, *separator;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    arr_length = njs_typed_array_length(array);

    separator = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_string(separator))) {
        if (njs_is_undefined(separator)) {
            separator = njs_value_arg(&njs_string_comma);

        } else {
            ret = njs_value_to_string(vm, separator, separator);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }
    }

    if (arr_length == 0) {
        vm->retval = njs_string_empty;
        return NJS_OK;
    }

    njs_chb_init(&chain, vm->mem_pool);

    length = njs_typed_array_to_chain(vm, &chain, array, separator);
    size = njs_chb_size(&chain);

    p = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    njs_chb_join_to(&chain, p);
    njs_chb_destroy(&chain);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_constructor_intrinsic(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_type_error(vm, "Abstract class TypedArray not directly constructable");

    return NJS_ERROR;
}


static const njs_object_prop_t  njs_typed_array_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("TypedArray"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 0, 0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_SPECIES),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_typed_array_get_this, 0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
        .enumerable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_constructor_init = {
    njs_typed_array_constructor_props,
    njs_nitems(njs_typed_array_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_typed_array_get_string_tag,
                                      0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
        .enumerable = 0,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("buffer"),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_typed_array_prototype_buffer, 0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
        .enumerable = 0,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("byteLength"),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_typed_array_prototype_byte_length, 0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
        .enumerable = 0,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("byteOffset"),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_typed_array_prototype_byte_offset, 0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
        .enumerable = 0,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_INVALID, 1, NAN),
        .getter = njs_native_function(njs_typed_array_prototype_length, 0),
        .setter = njs_value(NJS_UNDEFINED, 0, NAN),
        .writable = NJS_ATTRIBUTE_UNSET,
        .configurable = 1,
        .enumerable = 0,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("set"),
        .value = njs_native_function(njs_typed_array_prototype_set, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("slice"),
        .value = njs_native_function2(njs_typed_array_prototype_slice, 2, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("subarray"),
        .value = njs_native_function2(njs_typed_array_prototype_slice, 2, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("copyWithin"),
        .value = njs_native_function(njs_typed_array_prototype_copy_within, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("fill"),
        .value = njs_native_function(njs_typed_array_prototype_fill, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("join"),
        .value = njs_native_function(njs_typed_array_prototype_join, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_array_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },
};


static const njs_object_init_t  njs_typed_array_prototype_init = {
    njs_typed_array_prototype_properties,
    njs_nitems(njs_typed_array_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor_intrinsic, 0, 0),
    .prototype_props = &njs_typed_array_prototype_init,
    .constructor_props = &njs_typed_array_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_typed_array_u8_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Uint8Array"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 1),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_u8_constructor_init = {
    njs_typed_array_u8_constructor_props,
    njs_nitems(njs_typed_array_u8_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_u8_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 1),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_u8_prototype_init = {
    njs_typed_array_u8_prototype_properties,
    njs_nitems(njs_typed_array_u8_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_u8_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor, 3,
                                   NJS_OBJ_TYPE_UINT8_ARRAY),
    .prototype_props = &njs_typed_array_u8_prototype_init,
    .constructor_props = &njs_typed_array_u8_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_typed_array_u8c_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_long_string("Uint8ClampedArray"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 1),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_u8c_constructor_init = {
    njs_typed_array_u8c_constructor_props,
    njs_nitems(njs_typed_array_u8c_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_u8c_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 1),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_u8c_prototype_init = {
    njs_typed_array_u8c_prototype_properties,
    njs_nitems(njs_typed_array_u8c_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_u8clamped_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor, 3,
                                   NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY),
    .prototype_props = &njs_typed_array_u8c_prototype_init,
    .constructor_props = &njs_typed_array_u8c_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_typed_array_i8_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Int8Array"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 1),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_i8_constructor_init = {
    njs_typed_array_i8_constructor_props,
    njs_nitems(njs_typed_array_i8_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_i8_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 1),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_i8_prototype_init = {
    njs_typed_array_i8_prototype_properties,
    njs_nitems(njs_typed_array_i8_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_i8_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor, 3,
                                   NJS_OBJ_TYPE_INT8_ARRAY),
    .prototype_props = &njs_typed_array_i8_prototype_init,
    .constructor_props = &njs_typed_array_i8_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_typed_array_u16_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Uint16Array"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 2),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_u16_constructor_init = {
    njs_typed_array_u16_constructor_props,
    njs_nitems(njs_typed_array_u16_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_u16_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 2),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_u16_prototype_init = {
    njs_typed_array_u16_prototype_properties,
    njs_nitems(njs_typed_array_u16_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_u16_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor, 3,
                                   NJS_OBJ_TYPE_UINT16_ARRAY),
    .prototype_props = &njs_typed_array_u16_prototype_init,
    .constructor_props = &njs_typed_array_u16_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_typed_array_i16_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Int16Array"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 2),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_i16_constructor_init = {
    njs_typed_array_i16_constructor_props,
    njs_nitems(njs_typed_array_i16_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_i16_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 2),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_i16_prototype_init = {
    njs_typed_array_i16_prototype_properties,
    njs_nitems(njs_typed_array_i16_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_i16_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor, 3,
                                   NJS_OBJ_TYPE_INT16_ARRAY),
    .prototype_props = &njs_typed_array_i16_prototype_init,
    .constructor_props = &njs_typed_array_i16_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_typed_array_u32_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Uint32Array"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 4),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_u32_constructor_init = {
    njs_typed_array_u32_constructor_props,
    njs_nitems(njs_typed_array_u32_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_u32_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 4),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_u32_prototype_init = {
    njs_typed_array_u32_prototype_properties,
    njs_nitems(njs_typed_array_u32_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_u32_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor, 3,
                                   NJS_OBJ_TYPE_UINT32_ARRAY),
    .prototype_props = &njs_typed_array_u32_prototype_init,
    .constructor_props = &njs_typed_array_u32_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_typed_array_i32_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Int32Array"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 4),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_i32_constructor_init = {
    njs_typed_array_i32_constructor_props,
    njs_nitems(njs_typed_array_i32_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_i32_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 4),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_i32_prototype_init = {
    njs_typed_array_i32_prototype_properties,
    njs_nitems(njs_typed_array_i32_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_i32_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor, 3,
                                   NJS_OBJ_TYPE_INT32_ARRAY),
    .prototype_props = &njs_typed_array_i32_prototype_init,
    .constructor_props = &njs_typed_array_i32_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_typed_array_f32_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Float32Array"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 4),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_f32_constructor_init = {
    njs_typed_array_f32_constructor_props,
    njs_nitems(njs_typed_array_f32_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_f32_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 4),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_f32_prototype_init = {
    njs_typed_array_f32_prototype_properties,
    njs_nitems(njs_typed_array_f32_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_f32_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor, 3,
                                   NJS_OBJ_TYPE_FLOAT32_ARRAY),
    .prototype_props = &njs_typed_array_f32_prototype_init,
    .constructor_props = &njs_typed_array_f32_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_typed_array_f64_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Float64Array"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 8),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_f64_constructor_init = {
    njs_typed_array_f64_constructor_props,
    njs_nitems(njs_typed_array_f64_constructor_props),
};


static const njs_object_prop_t  njs_typed_array_f64_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("BYTES_PER_ELEMENT"),
        .value = njs_value(NJS_NUMBER, 1, 8),
        .configurable = 0,
        .enumerable = 0,
        .writable = 0,
    },
};


static const njs_object_init_t  njs_typed_array_f64_prototype_init = {
    njs_typed_array_f64_prototype_properties,
    njs_nitems(njs_typed_array_f64_prototype_properties),
};


const njs_object_type_init_t  njs_typed_array_f64_type_init = {
    .constructor = njs_native_ctor(njs_typed_array_constructor, 3,
                                   NJS_OBJ_TYPE_FLOAT64_ARRAY),
    .prototype_props = &njs_typed_array_f64_prototype_init,
    .constructor_props = &njs_typed_array_f64_constructor_init,
    .prototype_value = { .object = { .type = NJS_OBJECT } },
};
