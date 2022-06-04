
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef enum {
    NJS_ARRAY_EVERY = 0,
    NJS_ARRAY_FOR_EACH,
    NJS_ARRAY_SOME,
    NJS_ARRAY_FIND,
    NJS_ARRAY_FIND_INDEX,
    NJS_ARRAY_FILTER,
    NJS_ARRAY_MAP,
} njs_array_iterator_fun_t;


static void njs_typed_array_prop_set(njs_vm_t *vm, njs_typed_array_t *array,
    uint32_t index, double v);


njs_typed_array_t *
njs_typed_array_alloc(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_bool_t zeroing, njs_object_type_t type)
{
    double              num;
    int64_t             i, length;
    uint32_t            element_size;
    uint64_t            size, offset;
    njs_int_t           ret;
    njs_value_t         *value, prop;
    njs_typed_array_t   *array, *src_tarray;
    njs_array_buffer_t  *buffer;

    size = 0;
    length = 0;
    offset = 0;

    buffer = NULL;
    src_tarray = NULL;

    element_size = njs_typed_array_element_size(type);

    value = njs_arg(args, nargs, 0);

    if (njs_is_array_buffer(value)) {
        buffer = njs_array_buffer(value);

        ret = njs_value_to_index(vm, njs_arg(args, nargs, 1), &offset);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        if (njs_slow_path((offset % element_size) != 0)) {
            njs_range_error(vm, "start offset must be multiple of %uD",
                            element_size);
            return NULL;
        }

        if (njs_is_defined(njs_arg(args, nargs, 2))) {
            ret = njs_value_to_index(vm, njs_argument(args, 2), &size);
            if (njs_slow_path(ret != NJS_OK)) {
                return NULL;
            }
        }

        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NULL;
        }

        if (njs_is_defined(njs_arg(args, nargs, 2))) {
            ret = njs_value_to_index(vm, njs_argument(args, 2), &size);
            if (njs_slow_path(ret != NJS_OK)) {
                return NULL;
            }

            size *= element_size;

            if (njs_slow_path((offset + size) > buffer->size)) {
                njs_range_error(vm, "Invalid typed array length: %uL", size);
                return NULL;
            }

        } else {
            if (njs_slow_path((buffer->size % element_size) != 0)) {
                njs_range_error(vm, "byteLength of buffer must be "
                                "multiple of %uD", element_size);
                return NULL;
            }

            if (offset > buffer->size) {
                njs_range_error(vm, "byteOffset %uL is outside the bound of "
                                "the buffer", offset);
                return NULL;
            }

            size = buffer->size - offset;
        }

    } else if (njs_is_typed_array(value)) {
        src_tarray = njs_typed_array(value);
        if (njs_slow_path(njs_is_detached_buffer(src_tarray->buffer))) {
            njs_type_error(vm, "detached buffer");
            return NULL;
        }

        size = (uint64_t) njs_typed_array_length(src_tarray) * element_size;

    } else if (njs_is_object(value)) {
        ret = njs_object_length(vm, value, &length);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return NULL;
        }

        size = length * element_size;

    } else {
        ret = njs_value_to_index(vm, value, &size);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        size *= element_size;
    }

    if (buffer == NULL) {
        buffer = njs_array_buffer_alloc(vm, size, zeroing);
        if (njs_slow_path(buffer == NULL)) {
            return NULL;
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
                njs_typed_array_prop_set(vm, array, i,
                                         njs_typed_array_prop(src_tarray, i));
            }

        } else {
            memcpy(&buffer->u.u8[0], &src_tarray->buffer->u.u8[0], size);
        }

    } else if (!njs_is_array_buffer(value) && njs_is_object(value)) {
        for (i = 0; i < length; i++) {
            ret = njs_value_property_i64(vm, value, i, &prop);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return NULL;
            }

            num = NAN;

            if (ret == NJS_OK) {
                ret = njs_value_to_number(vm, &prop, &num);
                if (njs_slow_path(ret == NJS_ERROR)) {
                    return NULL;
                }
            }

            njs_typed_array_prop_set(vm, array, i, num);
        }
    }

    njs_lvlhsh_init(&array->object.hash);
    njs_lvlhsh_init(&array->object.shared_hash);
    array->object.__proto__ = &vm->prototypes[type].object;
    array->object.type = NJS_TYPED_ARRAY;
    array->object.extensible = 1;
    array->object.fast_array = 1;

    return array;

memory_error:

    njs_memory_error(vm);

    return NULL;
}


static njs_int_t
njs_typed_array_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
{
    njs_typed_array_t  *array;

    if (!vm->top_frame->ctor) {
        njs_type_error(vm, "Constructor of TypedArray requires 'new'");
        return NJS_ERROR;
    }

    array = njs_typed_array_alloc(vm, &args[1], nargs - 1, 1, magic);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    njs_set_typed_array(&vm->retval, array);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_create(njs_vm_t *vm, njs_value_t *constructor,
    njs_value_t *args, njs_uint_t nargs, njs_value_t *retval)
{
    njs_int_t          ret;
    njs_value_t        this;
    njs_object_t       *object;
    njs_typed_array_t  *array;

    object = njs_function_new_object(vm, constructor);
    if (njs_slow_path(object == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&this, object);

    ret = njs_function_call2(vm, njs_function(constructor), &this, args,
                             nargs, retval, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_typed_array(retval))) {
        njs_type_error(vm, "Derived TypedArray constructor "
                       "returned not a typed array");
        return NJS_ERROR;
    }

    array = njs_typed_array(retval);
    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    if (njs_slow_path(nargs == 1 && njs_is_number(&args[0])
                      && njs_typed_array_length(array)
                         < njs_number(&args[0])))
    {
        njs_type_error(vm, "Derived TypedArray constructor "
                       "returned too short array");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_typed_array_species_create(njs_vm_t *vm, njs_value_t *exemplar,
    njs_value_t *args, njs_uint_t nargs, njs_value_t *retval)
{
    njs_int_t          ret;
    njs_value_t        constructor;
    njs_typed_array_t  *array;

    array = njs_typed_array(exemplar);

    njs_set_function(&constructor, &vm->constructors[array->type]);

    ret = njs_value_species_constructor(vm, exemplar, &constructor,
                                        &constructor);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return njs_typed_array_create(vm, &constructor, args, nargs, retval);
}


static njs_int_t
njs_typed_array_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    uint32_t           length, i;
    njs_int_t          ret;
    njs_value_t        *this;
    njs_value_t        argument;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_constructor(this))) {
        njs_type_error(vm, "%s is not a constructor",
                       njs_type_string(this->type));
        return NJS_ERROR;
    }

    length = nargs - 1;

    njs_set_number(&argument, length);
    ret = njs_typed_array_create(vm, this, &argument, 1, &vm->retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    array = njs_typed_array(&vm->retval);

    for (i = 0; i < length; i++) {
        ret = njs_value_to_number(vm, njs_argument(args, i + 1), &num);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        njs_typed_array_prop_set(vm, array, i, num);
    }

    njs_set_typed_array(&vm->retval, array);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_from(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    int64_t            length, i;
    njs_int_t          ret;
    njs_value_t        *this, *source, *mapfn;
    njs_value_t        arguments[3], retval;
    njs_function_t     *function;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_constructor(this))) {
        njs_type_error(vm, "%s is not a constructor",
                       njs_type_string(this->type));
        return NJS_ERROR;
    }

    mapfn = njs_arg(args, nargs, 2);

    if (njs_slow_path(!njs_is_function_or_undefined(mapfn))) {
        njs_type_error(vm, "\"mapfn\" argument is not callable");
        return NJS_ERROR;
    }

    function = NULL;
    if (njs_is_function(mapfn)) {
        function = njs_function(mapfn);
    }

    source = njs_arg(args, nargs, 1);

    ret = njs_value_to_object(vm, source);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_object_length(vm, source, &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    njs_set_number(&arguments[0], length);
    ret = njs_typed_array_create(vm, this, arguments, 1, &vm->retval);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    array = njs_typed_array(&vm->retval);
    arguments[0] = *njs_arg(args, nargs, 3);

    for (i = 0; i < length; i++) {
        ret = njs_value_property_i64(vm, source, i, &retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return NJS_ERROR;
        }

        if (function != NULL) {
            arguments[1] = retval;
            njs_set_number(&arguments[2], i);
            ret = njs_function_apply(vm, function, arguments, 3, &retval);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }
        }

        ret = njs_value_to_number(vm, &retval, &num);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        njs_typed_array_prop_set(vm, array, i, num);
    }

    njs_set_typed_array(&vm->retval, array);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_get_this(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    vm->retval = args[0];

    return NJS_OK;
}


njs_array_buffer_t *
njs_typed_array_writable(njs_vm_t *vm, njs_typed_array_t *array)
{
    njs_int_t           ret;
    njs_array_buffer_t  *buffer;

    buffer = array->buffer;
    if (njs_slow_path(njs_is_detached_buffer(buffer))) {
        njs_type_error(vm, "detached buffer");
        return NULL;
    }

    ret = njs_array_buffer_writable(vm, buffer);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    return buffer;
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
    uint32_t           length;
    njs_value_t        *this;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (!njs_is_typed_array(this)) {
        njs_type_error(vm, "Method TypedArray.prototype.length called "
                       "on incompatible receiver");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    length = njs_typed_array_length(array);

    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        length = 0;
    }

    njs_set_number(&vm->retval, length);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_buffer(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_value_t        *this;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (!njs_is_typed_array(this) && !njs_is_data_view(this)) {
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
    size_t             byte_length;
    njs_value_t        *this;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (!njs_is_typed_array(this) && !njs_is_data_view(this)) {
        njs_type_error(vm, "Method TypedArray.prototype.byteLength called "
                       "on incompatible receiver");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    byte_length = array->byte_length;

    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        if (njs_is_data_view(this)) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        byte_length = 0;
    }

    njs_set_number(&vm->retval, byte_length);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_byte_offset(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    size_t             byte_offset;
    njs_value_t        *this;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (!njs_is_typed_array(this) && !njs_is_data_view(this)) {
        njs_type_error(vm, "Method TypedArray.prototype.byteOffset called "
                       "on incompatible receiver");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    byte_offset = njs_typed_array_offset(array);

    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        if (njs_is_data_view(this)) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        byte_offset = 0;
    }

    njs_set_number(&vm->retval, byte_offset);

    return NJS_OK;
}


static void
njs_typed_array_prop_set(njs_vm_t *vm, njs_typed_array_t *array, uint32_t index,
    double v)
{
    int8_t              i8;
    int16_t             i16;
    int32_t             i32;
    njs_array_buffer_t  *buffer;

    buffer = array->buffer;
    index += array->offset;

    njs_assert(!buffer->object.shared);

    switch (array->type) {
    case NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY:
        if (isnan(v) || v < 0) {
            v = 0;
        } else if (v > 255) {
            v = 255;
        }

        buffer->u.u8[index] = lrint(v);

        break;

    case NJS_OBJ_TYPE_UINT8_ARRAY:
    case NJS_OBJ_TYPE_INT8_ARRAY:
        i8 = njs_number_to_int32(v);
        buffer->u.u8[index] = i8;
        break;

    case NJS_OBJ_TYPE_UINT16_ARRAY:
    case NJS_OBJ_TYPE_INT16_ARRAY:
        i16 = njs_number_to_int32(v);
        buffer->u.u16[index] = i16;
        break;

    case NJS_OBJ_TYPE_UINT32_ARRAY:
    case NJS_OBJ_TYPE_INT32_ARRAY:
        i32 = njs_number_to_int32(v);
        buffer->u.u32[index] = i32;
        break;

    case NJS_OBJ_TYPE_FLOAT32_ARRAY:
        buffer->u.f32[index] = v;
        break;

    default:

        /* NJS_OBJ_TYPE_FLOAT64_ARRAY. */

        buffer->u.f64[index] = v;
    }
}


njs_int_t
njs_typed_array_set_value(njs_vm_t *vm, njs_typed_array_t *array,
    uint32_t index, njs_value_t *setval)
{
    double              num;
    njs_int_t           ret;
    njs_array_buffer_t  *buffer;

    ret = njs_value_to_number(vm, setval, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    njs_typed_array_prop_set(vm, array, index, num);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_set(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double              num;
    int64_t             i, length, src_length, offset;
    njs_int_t           ret;
    njs_value_t         *this, *src, *value, prop;
    njs_array_t         *array;
    njs_typed_array_t   *self, *src_tarray;
    njs_array_buffer_t  *buffer;

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

    buffer = njs_typed_array_writable(vm, self);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    length = njs_typed_array_length(self);

    if (njs_is_typed_array(src)) {
        src_tarray = njs_typed_array(src);
        if (njs_slow_path(njs_is_detached_buffer(src_tarray->buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        src_length = njs_typed_array_length(src_tarray);

        if (njs_slow_path((src_length > length)
                          || (offset > length - src_length)))
        {
            njs_range_error(vm, "source is too large");
            return NJS_ERROR;
        }

        length = njs_min(njs_typed_array_length(src_tarray), length - offset);

        for (i = 0; i < length; i++) {
            njs_typed_array_prop_set(vm, self, offset + i,
                                     njs_typed_array_prop(src_tarray, i));
        }

    } else {
        if (njs_is_fast_array(src)) {
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
                    njs_typed_array_prop_set(vm, self, offset + i, num);
                }
            }

            goto done;
        }

        ret = njs_value_to_object(vm, src);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_object_length(vm, src, &src_length);
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

            if (njs_slow_path(njs_is_detached_buffer(buffer))) {
                njs_type_error(vm, "detached buffer");
                return NJS_ERROR;
            }

            njs_typed_array_prop_set(vm, self, offset + i, num);
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
    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

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

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    njs_set_typed_array(&vm->retval, array);

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


njs_int_t
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
    if (njs_slow_path(copy && njs_is_detached_buffer(buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

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

        if (count == 0) {
            return NJS_OK;
        }

        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        new_array = njs_typed_array(&vm->retval);
        new_buffer = njs_typed_array_buffer(new_array);
        element_size = njs_typed_array_element_size(array->type);

        if (njs_fast_path(array->type == new_array->type)) {
            start = start * element_size;
            count = count * element_size;

            memcpy(&new_buffer->u.u8[0], &buffer->u.u8[start], count);

        } else {
            for (i = 0; i < count; i++) {
                njs_typed_array_prop_set(vm, new_array, i,
                                        njs_typed_array_prop(array, i + start));
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
    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

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

    final = length;

    if (njs_is_defined(value)) {
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

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    element_size = njs_typed_array_element_size(array->type);

    to = (to + array->offset) * element_size;
    from = (from + array->offset) * element_size;
    count = count * element_size;

    memmove(&buffer->u.u8[to], &buffer->u.u8[from], count);

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_iterator(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t type)
{
    double              val;
    int64_t             i, length;
    njs_int_t           ret;
    njs_arr_t           results;
    njs_value_t         *this, *this_arg, *r;
    njs_value_t         arguments[4], retval;
    njs_function_t      *function;
    njs_typed_array_t   *array, *dst;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    dst = NULL;
    array = njs_typed_array(this);
    length = njs_typed_array_length(array);

    if (njs_slow_path(!njs_is_function(njs_arg(args, nargs, 1)))) {
        njs_type_error(vm, "callback argument is not callable");
        return NJS_ERROR;
    }

    function = njs_function(njs_argument(args, 1));
    this_arg = njs_arg(args, nargs, 2);

    buffer = array->buffer;
    results.separate = 0;
    results.pointer = 0;

    switch (type) {
    case NJS_ARRAY_MAP:
        njs_set_number(&arguments[0], length);
        ret = njs_typed_array_species_create(vm, this, njs_value_arg(arguments),
                                             1, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        dst = njs_typed_array(&retval);
        break;

    case NJS_ARRAY_FILTER:
    default:
        r = njs_arr_init(vm->mem_pool, &results, NULL, 4, sizeof(njs_value_t));
        if (njs_slow_path(r == NULL)) {
            return NJS_ERROR;
        }
    }

    for (i = 0; i < length; i++) {
        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        val = njs_typed_array_prop(array, i);

        arguments[0] = *this_arg;
        njs_set_number(&arguments[1], val);
        njs_set_number(&arguments[2], i);
        njs_set_typed_array(&arguments[3], array);

        ret = njs_function_apply(vm, function, arguments, 4, &vm->retval);
        if (njs_slow_path(ret != NJS_OK)) {
            goto exception;
        }

        switch (type) {
        case NJS_ARRAY_EVERY:
            if (!njs_is_true(&vm->retval)) {
                vm->retval = njs_value_false;
                goto done;
            }

            break;

        case NJS_ARRAY_FOR_EACH:
            break;

        case NJS_ARRAY_SOME:
        case NJS_ARRAY_FIND:
        case NJS_ARRAY_FIND_INDEX:
            if (!njs_is_true(&vm->retval)) {
                continue;
            }

            switch (type) {
            case NJS_ARRAY_SOME:
                vm->retval = njs_value_true;
                break;

            case NJS_ARRAY_FIND:
                njs_set_number(&vm->retval, val);
                break;

            default:
                njs_set_number(&vm->retval, i);
                break;
            }

            goto done;

        case NJS_ARRAY_MAP:
            ret = njs_typed_array_set_value(vm, dst, i, &vm->retval);
            if (njs_slow_path(ret != NJS_OK)) {
                goto exception;
            }

            break;

        default:
            /* NJS_ARRAY_FILTER. */

            if (!njs_is_true(&vm->retval)) {
                continue;
            }

            r = njs_arr_add(&results);
            if (njs_slow_path(r == NULL)) {
                goto exception;
            }

            njs_set_number(r, val);
        }
    }

    /* Default values. */

    switch (type) {
    case NJS_ARRAY_EVERY:
        vm->retval = njs_value_true;
        break;

    case NJS_ARRAY_SOME:
        vm->retval = njs_value_false;
        break;

    case NJS_ARRAY_FOR_EACH:
    case NJS_ARRAY_FIND:
        njs_set_undefined(&vm->retval);
        break;

    case NJS_ARRAY_FIND_INDEX:
        njs_set_number(&vm->retval, -1);
        break;

    case NJS_ARRAY_MAP:
    case NJS_ARRAY_FILTER:
    default:
        if (type == NJS_ARRAY_FILTER) {
            njs_set_number(&arguments[0], results.items);
            ret = njs_typed_array_species_create(vm, this,
                                                 njs_value_arg(arguments),
                                                 1, &retval);
            if (njs_slow_path(ret != NJS_OK)) {
                goto exception;
            }

            dst = njs_typed_array(&retval);

            i = 0;

            while (i < results.items) {
                r = njs_arr_item(&results, i);
                ret = njs_typed_array_set_value(vm, dst, i++, r);
                if (njs_slow_path(ret != NJS_OK)) {
                    goto exception;
                }
            }
        }

        njs_set_typed_array(&vm->retval, dst);
        break;
    }

done:

    ret = NJS_OK;

exception:

    njs_arr_destroy(&results);

    return ret;
}


static njs_int_t
njs_typed_array_prototype_index_of(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t type)
{
    double              v;
    int64_t             i, i64, from, to, index, increment, offset, length;
    njs_int_t           ret, integer;
    njs_value_t         *this;
    const float         *f32;
    const double        *f64;
    const uint8_t       *u8;
    const uint16_t      *u16;
    const uint32_t      *u32;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    index = -1;
    array = njs_typed_array(this);
    length = njs_typed_array_length(array);

    if (!njs_is_number(njs_arg(args, nargs, 1)) || length == 0) {
        goto done;
    }

    if (type & 2) {
        /* lastIndexOf(). */

        if (nargs > 2) {
            ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &from);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

        } else {
            from = length - 1;
        }

        if (from >= 0) {
            from = njs_min(from, length - 1);

        } else if (from < 0) {
            from += length;
        }

        to = -1;
        increment = -1;

        if (from <= to) {
            goto done;
        }

    } else {
        /* indexOf(), includes(). */

        ret = njs_value_to_integer(vm, njs_arg(args, nargs, 2), &from);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (from < 0) {
            from += length;

            if (from < 0) {
                from = 0;
            }
        }

        to = length;
        increment = 1;

        if (from >= to) {
            goto done;
        }
    }

    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    v = njs_number(njs_argument(args, 1));

    i64 = v;
    integer = (v == i64);

    buffer = array->buffer;
    offset = array->offset;

    switch (array->type) {
    case NJS_OBJ_TYPE_INT8_ARRAY:
        if (integer && ((int8_t) i64 == i64)) {
            goto search8;
        }

        break;

    case NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY:
    case NJS_OBJ_TYPE_UINT8_ARRAY:
        if (integer && ((uint8_t) i64 == i64)) {
search8:
            u8 = &buffer->u.u8[0];
            for (i = from; i != to; i += increment) {
                if (u8[offset + i] == (uint8_t) i64) {
                    index = i;
                    break;
                }
            }
        }

        break;

    case NJS_OBJ_TYPE_INT16_ARRAY:
        if (integer && ((int16_t) i64 == i64)) {
            goto search16;
        }

        break;

    case NJS_OBJ_TYPE_UINT16_ARRAY:
        if (integer && ((uint16_t) i64 == i64)) {
search16:
            u16 = &buffer->u.u16[0];
            for (i = from; i != to; i += increment) {
                if (u16[offset + i] == (uint16_t) i64) {
                    index = i;
                    break;
                }
            }
        }

        break;

    case NJS_OBJ_TYPE_INT32_ARRAY:
        if (integer && ((int32_t) i64 == i64)) {
            goto search32;
        }

        break;

    case NJS_OBJ_TYPE_UINT32_ARRAY:
        if (integer && ((uint32_t) i64 == i64)) {
search32:
            u32 = &buffer->u.u32[0];
            for (i = from; i != to; i += increment) {
                if (u32[offset + i] == (uint32_t) i64) {
                    index = i;
                    break;
                }
            }
        }

        break;

    case NJS_OBJ_TYPE_FLOAT32_ARRAY:
        f32 = &buffer->u.f32[0];

        if (((float) v == v)) {
            for (i = from; i != to; i += increment) {
                if (f32[offset + i] == (float) v) {
                    index = i;
                    break;
                }
            }

        } else if ((type & 1) && isnan(v)) {
            /* includes() handles NaN. */

            for (i = from; i != to; i += increment) {
                if (isnan(f32[offset + i])) {
                    index = i;
                    break;
                }
            }
        }

        break;

    default:

        /* NJS_OBJ_TYPE_FLOAT64_ARRAY. */

        f64 = &buffer->u.f64[0];

        if ((type & 1) && isnan(v)) {
            /* includes() handles NaN. */

            for (i = from; i != to; i += increment) {
                if (isnan(f64[offset + i])) {
                    index = i;
                    break;
                }
            }

        } else {
            for (i = from; i != to; i += increment) {
                if (f64[offset + i] == v) {
                    index = i;
                    break;
                }
            }
        }
    }

done:

    /* Default values. */

    if (type & 1) {
        njs_set_boolean(&vm->retval, index != -1);

    } else {
        njs_set_number(&vm->retval, index);
    }

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_reduce(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t right)
{
    int64_t             i, from, to, increment, length;
    njs_int_t           ret;
    njs_value_t         *this, accumulator;
    njs_value_t         arguments[5];
    njs_function_t      *function;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    length = njs_typed_array_length(array);

    if (njs_slow_path(!njs_is_function(njs_arg(args, nargs, 1)))) {
        njs_type_error(vm, "callback argument is not callable");
        return NJS_ERROR;
    }

    function = njs_function(njs_argument(args, 1));

    if (length == 0 && nargs <= 2) {
        njs_type_error(vm, "Reduce of empty object with no initial value");
        return NJS_ERROR;
    }

    if (right) {
        from = length - 1;
        to = -1;
        increment = -1;

    } else {
        from = 0;
        to = length;
        increment = 1;
    }

    buffer = array->buffer;

    if (nargs > 2) {
        accumulator = *njs_argument(args, 2);

    } else {
        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        njs_set_number(&accumulator, njs_typed_array_prop(array, from));
        from += increment;
    }

    for (i = from; i != to; i += increment) {
        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        njs_set_undefined(&arguments[0]);
        arguments[1] = accumulator;
        njs_set_number(&arguments[2], njs_typed_array_prop(array, i));
        njs_set_number(&arguments[3], i);
        njs_set_typed_array(&arguments[4], array);

        ret =  njs_function_apply(vm, function, arguments, 5, &accumulator);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    vm->retval = accumulator;

    return NJS_OK;
}


static njs_int_t
njs_typed_array_prototype_reverse(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double              *f64;
    uint8_t             *u8;
    int64_t             i, length;
    uint16_t            *u16;
    uint32_t            *u32;
    njs_value_t         *this;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    length = njs_typed_array_length(array);

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    switch (array->type) {
    case NJS_OBJ_TYPE_UINT8_ARRAY:
    case NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY:
    case NJS_OBJ_TYPE_INT8_ARRAY:
        u8 = &buffer->u.u8[array->offset];

        for (i = 0; i < length / 2; i++) {
            njs_swap_u8(&u8[i], &u8[length - i - 1], 0);
        }

        break;

    case NJS_OBJ_TYPE_INT16_ARRAY:
    case NJS_OBJ_TYPE_UINT16_ARRAY:
        u16 = &buffer->u.u16[array->offset];

        for (i = 0; i < length / 2; i++) {
            njs_swap_u16(&u16[i], &u16[length - i - 1], 0);
        }

        break;

    case NJS_OBJ_TYPE_INT32_ARRAY:
    case NJS_OBJ_TYPE_UINT32_ARRAY:
    case NJS_OBJ_TYPE_FLOAT32_ARRAY:
        u32 = &buffer->u.u32[array->offset];

        for (i = 0; i < length / 2; i++) {
            njs_swap_u32(&u32[i], &u32[length - i - 1], 0);
        }

        break;

    default:

        /* NJS_OBJ_TYPE_FLOAT64_ARRAY. */

        f64 = &buffer->u.f64[array->offset];

        for (i = 0; i < length / 2; i++) {
            njs_swap_u64(&f64[i], &f64[length - i - 1], 0);
        }
    }

    njs_set_typed_array(&vm->retval, array);

    return NJS_OK;
}


static int
njs_typed_array_compare_i8(const void *a, const void *b, void *c)
{
    return *((const int8_t *) a) - *((const int8_t *) b);
}


static int
njs_typed_array_compare_u8(const void *a, const void *b, void *c)
{
    return *((const uint8_t *) a) - *((const uint8_t *) b);
}


static int
njs_typed_array_compare_i16(const void *a, const void *b, void *c)
{
    return *((const int16_t *) a) - *((const int16_t *) b);
}


static int
njs_typed_array_compare_u16(const void *a, const void *b, void *c)
{
    return *((const uint16_t *) a) - *((const uint16_t *) b);
}


static int
njs_typed_array_compare_i32(const void *a, const void *b, void *c)
{
    int32_t  ai, bi;

    ai = *(const int32_t *) a;
    bi = *(const int32_t *) b;

    return (ai > bi) - (ai < bi);
}


static int
njs_typed_array_compare_u32(const void *a, const void *b, void *c)
{
    uint32_t  au, bu;

    au = *(const uint32_t *) a;
    bu = *(const uint32_t *) b;

    return (au > bu) - (au < bu);
}


njs_inline int
njs_typed_array_compare(double a, double b)
{
    if (njs_slow_path(isnan(a))) {
        if (isnan(b)) {
            return 0;
        }

        return 1;
    }

    if (njs_slow_path(isnan(b))) {
        return -1;
    }

    if (a < b) {
        return -1;
    }

    if (a > b) {
        return 1;
    }

    return signbit(b) - signbit(a);
}


static int
njs_typed_array_compare_f32(const void *a, const void *b, void *c)
{
    return njs_typed_array_compare(*(const float *) a, *(const float *) b);
}


static int
njs_typed_array_compare_f64(const void *a, const void *b, void *c)
{
    return njs_typed_array_compare(*(const double *) a, *(const double *) b);
}


static double
njs_typed_array_get_u8(const void *a)
{
    return *(const uint8_t *) a;
}


static double
njs_typed_array_get_i8(const void *a)
{
    return *(const int8_t *) a;
}


static double
njs_typed_array_get_u16(const void *a)
{
    return *(const uint16_t *) a;
}


static double
njs_typed_array_get_i16(const void *a)
{
    return *(const int16_t *) a;
}


static double
njs_typed_array_get_u32(const void *a)
{
    return *(const uint32_t *) a;
}


static double
njs_typed_array_get_i32(const void *a)
{
    return *(const int32_t *) a;
}


static double
njs_typed_array_get_f32(const void *a)
{
    return *(const float *) a;
}


static double
njs_typed_array_get_f64(const void *a)
{
    return *(const double *) a;
}


typedef struct {
    njs_vm_t               *vm;
    njs_array_buffer_t     *buffer;
    njs_function_t         *function;
    njs_bool_t             exception;
    double                 (*get)(const void *v);
} njs_typed_array_sort_ctx_t;

typedef int (*njs_typed_array_cmp_t)(const void *, const void *, void *ctx);


static int
njs_typed_array_generic_compare(const void *a, const void *b, void *c)
{
    double                      num;
    njs_int_t                   ret;
    njs_value_t                 arguments[3], retval;
    njs_typed_array_sort_ctx_t  *ctx;

    ctx = c;

    if (njs_slow_path(ctx->exception)) {
        return 0;
    }

    njs_set_undefined(&arguments[0]);
    njs_set_number(&arguments[1], ctx->get(a));
    njs_set_number(&arguments[2], ctx->get(b));

    ret = njs_function_apply(ctx->vm, ctx->function, arguments, 3, &retval);
    if (njs_slow_path(ret != NJS_OK)) {
        goto exception;
    }

    ret = njs_value_to_number(ctx->vm, &retval, &num);
    if (njs_slow_path(ret != NJS_OK)) {
        goto exception;
    }

    if (njs_slow_path(njs_is_detached_buffer(ctx->buffer))) {
        njs_type_error(ctx->vm, "detached buffer");
        goto exception;
    }

    if (njs_slow_path(isnan(num))) {
        return 0;
    }

    if (num != 0) {
        return (num > 0) - (num < 0);
    }

    return 0;

exception:

    ctx->exception = 1;

    return 0;
}


static njs_int_t
njs_typed_array_prototype_sort(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    u_char                      *base, *orig;
    int64_t                     length;
    uint32_t                    element_size;
    njs_value_t                 *this, *comparefn;
    njs_typed_array_t           *array;
    njs_array_buffer_t          *buffer;
    njs_typed_array_cmp_t       cmp;
    njs_typed_array_sort_ctx_t  ctx;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    ctx.vm = vm;
    ctx.buffer = array->buffer;
    ctx.exception = 0;

    comparefn = njs_arg(args, nargs, 1);

    if (njs_is_defined(comparefn)) {
        if (njs_slow_path(!njs_is_function(comparefn))) {
            njs_type_error(vm, "comparefn must be callable or undefined");
            return NJS_ERROR;
        }

        ctx.function = njs_function(comparefn);

    } else {
        ctx.function = NULL;
    }

    switch (array->type) {
    case NJS_OBJ_TYPE_UINT8_ARRAY:
    case NJS_OBJ_TYPE_UINT8_CLAMPED_ARRAY:
        cmp = njs_typed_array_compare_u8;
        ctx.get = njs_typed_array_get_u8;
        break;

    case NJS_OBJ_TYPE_INT8_ARRAY:
        cmp = njs_typed_array_compare_i8;
        ctx.get = njs_typed_array_get_i8;
        break;

    case NJS_OBJ_TYPE_UINT16_ARRAY:
        cmp = njs_typed_array_compare_u16;
        ctx.get = njs_typed_array_get_u16;
        break;

    case NJS_OBJ_TYPE_INT16_ARRAY:
        cmp = njs_typed_array_compare_i16;
        ctx.get = njs_typed_array_get_i16;
        break;

    case NJS_OBJ_TYPE_UINT32_ARRAY:
        cmp = njs_typed_array_compare_u32;
        ctx.get = njs_typed_array_get_u32;
        break;

    case NJS_OBJ_TYPE_INT32_ARRAY:
        cmp = njs_typed_array_compare_i32;
        ctx.get = njs_typed_array_get_i32;
        break;

    case NJS_OBJ_TYPE_FLOAT32_ARRAY:
        cmp = njs_typed_array_compare_f32;
        ctx.get = njs_typed_array_get_f32;
        break;

    default:

        /* NJS_OBJ_TYPE_FLOAT64_ARRAY. */

        cmp = njs_typed_array_compare_f64;
        ctx.get = njs_typed_array_get_f64;
        break;
    }

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    length = njs_typed_array_length(array);
    element_size = njs_typed_array_element_size(array->type);
    base = &buffer->u.u8[array->offset * element_size];
    orig = base;

    if (ctx.function != NULL) {
        cmp = njs_typed_array_generic_compare;
        base = njs_mp_alloc(vm->mem_pool, length * element_size);
        if (njs_slow_path(base == NULL)) {
            njs_memory_error(vm);
            return NJS_ERROR;
        }

        memcpy(base, &buffer->u.u8[array->offset * element_size],
               length * element_size);
    }

    njs_qsort(base, length, element_size, cmp, &ctx);
    if (ctx.function != NULL) {
        if (&buffer->u.u8[array->offset * element_size] == orig) {
            memcpy(orig, base, length * element_size);
        }

        njs_mp_free(vm->mem_pool, base);
    }

    if (njs_slow_path(ctx.exception)) {
        return NJS_ERROR;
    }

    njs_set_typed_array(&vm->retval, array);

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
        njs_number_to_chain(vm, chain, njs_typed_array_prop(array, i));
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
    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

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

    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
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
njs_typed_array_prototype_iterator_obj(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t kind)
{
    njs_value_t        *this;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_typed_array(this))) {
        njs_type_error(vm, "this is not a typed array");
        return NJS_ERROR;
    }

    array = njs_typed_array(this);
    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    return njs_array_iterator_create(vm, this, &vm->retval, kind);
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

    {
        .type = NJS_PROPERTY,
        .name = njs_string("of"),
        .value = njs_native_function(njs_typed_array_of, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("from"),
        .value = njs_native_function(njs_typed_array_from, 1),
        .writable = 1,
        .configurable = 1,
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
        .name = njs_string("copyWithin"),
        .value = njs_native_function(njs_typed_array_prototype_copy_within, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("entries"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator_obj, 0,
                                      NJS_ENUM_BOTH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("every"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator, 1,
                                      NJS_ARRAY_EVERY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("filter"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator, 1,
                                      NJS_ARRAY_FILTER),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("find"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator, 1,
                                      NJS_ARRAY_FIND),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("findIndex"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator, 1,
                                      NJS_ARRAY_FIND_INDEX),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("forEach"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator, 1,
                                      NJS_ARRAY_FOR_EACH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("includes"),
        .value = njs_native_function2(njs_typed_array_prototype_index_of, 1, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("indexOf"),
        .value = njs_native_function2(njs_typed_array_prototype_index_of, 1, 0),
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
        .name = njs_string("fill"),
        .value = njs_native_function(njs_typed_array_prototype_fill, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("keys"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator_obj, 0,
                                      NJS_ENUM_KEYS),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("lastIndexOf"),
        .value = njs_native_function2(njs_typed_array_prototype_index_of, 1, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("map"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator, 1,
                                      NJS_ARRAY_MAP),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("reduce"),
        .value = njs_native_function2(njs_typed_array_prototype_reduce, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("reduceRight"),
        .value = njs_native_function2(njs_typed_array_prototype_reduce, 1, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("reverse"),
        .value = njs_native_function(njs_typed_array_prototype_reverse, 0),
        .writable = 1,
        .configurable = 1,
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
        .name = njs_string("some"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator, 1,
                                      NJS_ARRAY_SOME),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sort"),
        .value = njs_native_function(njs_typed_array_prototype_sort, 1),
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
        .name = njs_string("toString"),
        .value = njs_native_function(njs_array_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("values"),
        .value = njs_native_function2(njs_typed_array_prototype_iterator_obj, 0,
                                      NJS_ENUM_VALUES),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_ITERATOR),
        .value = njs_native_function2(njs_typed_array_prototype_iterator_obj, 0,
                                      NJS_ENUM_VALUES),
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


static njs_int_t
njs_data_view_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint64_t            size, offset;
    njs_int_t           ret;
    njs_data_view_t     *view;
    njs_array_buffer_t  *buffer;

    if (!vm->top_frame->ctor) {
        njs_type_error(vm, "Constructor of DataView requires 'new'");
        return NJS_ERROR;
    }

    if (!njs_is_array_buffer(njs_arg(args, nargs, 1))) {
        njs_type_error(vm, "buffer is not an ArrayBuffer");
        return NJS_ERROR;
    }

    size = 0;
    offset = 0;

    ret = njs_value_to_index(vm, njs_arg(args, nargs, 2), &offset);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    buffer = njs_array_buffer(njs_argument(args, 1));
    if (njs_slow_path(njs_is_detached_buffer(buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    if (!njs_is_undefined(njs_arg(args, nargs, 3))) {
        ret = njs_value_to_index(vm, njs_argument(args, 3), &size);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        if (njs_slow_path((offset + size) > buffer->size)) {
            njs_range_error(vm, "Invalid DataView length: %uL", size);
            return NJS_ERROR;
        }

    } else {
        if (offset > buffer->size) {
            njs_range_error(vm, "byteOffset %uL is outside the bound of "
                            "the buffer", offset);
            return NJS_ERROR;
        }

        size = buffer->size - offset;
    }

    view = njs_mp_zalloc(vm->mem_pool, sizeof(njs_data_view_t));
    if (njs_slow_path(view == NULL)) {
        goto memory_error;
    }

    view->buffer = buffer;
    view->offset = offset;
    view->byte_length = size;
    view->type = NJS_OBJ_TYPE_DATA_VIEW;

    njs_lvlhsh_init(&view->object.hash);
    njs_lvlhsh_init(&view->object.shared_hash);
    view->object.__proto__ = &vm->prototypes[view->type].object;
    view->object.type = NJS_DATA_VIEW;
    view->object.extensible = 1;

    njs_set_data_view(&vm->retval, view);

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NJS_ERROR;
}


static const njs_object_prop_t  njs_data_view_constructor_props[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("DataView"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


static const njs_object_init_t  njs_data_view_constructor_init = {
    njs_data_view_constructor_props,
    njs_nitems(njs_data_view_constructor_props),
};


static njs_int_t
njs_data_view_prototype_get(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t type)
{
    double              v;
    uint32_t            u32;
    uint64_t            index;
    njs_int_t           ret;
    njs_bool_t          swap;
    njs_value_t         *this;
    const uint8_t       *u8;
    njs_conv_f32_t      conv_f32;
    njs_conv_f64_t      conv_f64;
    njs_data_view_t     *view;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_data_view(this))) {
        njs_type_error(vm, "this is not a DataView");
        return NJS_ERROR;
    }

    ret = njs_value_to_index(vm, njs_arg(args, nargs, 1), &index);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    swap = njs_bool(njs_arg(args, nargs, 2));

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    view = njs_data_view(this);
    if (njs_slow_path(njs_is_detached_buffer(view->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    if (njs_typed_array_element_size(type) + index > view->byte_length) {
        njs_range_error(vm, "index %uL is outside the bound of the buffer",
                        index);
        return NJS_ERROR;
    }

    buffer = view->buffer;
    u8 = &buffer->u.u8[index + view->offset];

    switch (type) {
    case NJS_OBJ_TYPE_UINT8_ARRAY:
        v = *u8;
        break;

    case NJS_OBJ_TYPE_INT8_ARRAY:
        v = (int8_t) *u8;
        break;

    case NJS_OBJ_TYPE_UINT16_ARRAY:
        u32 = njs_get_u16(u8);

        if (swap) {
            u32 = njs_bswap_u16(u32);
        }

        v = u32;
        break;

    case NJS_OBJ_TYPE_INT16_ARRAY:
        u32 = njs_get_u16(u8);

        if (swap) {
            u32 = njs_bswap_u16(u32);
        }

        v = (int16_t) u32;
        break;

    case NJS_OBJ_TYPE_UINT32_ARRAY:
    case NJS_OBJ_TYPE_INT32_ARRAY:
    case NJS_OBJ_TYPE_FLOAT32_ARRAY:
        u32 = njs_get_u32(u8);

        if (swap) {
            u32 = njs_bswap_u32(u32);
        }

        switch (type) {
        case NJS_OBJ_TYPE_UINT32_ARRAY:
            v = u32;
            break;

        case NJS_OBJ_TYPE_INT32_ARRAY:
            v = (int32_t) u32;
            break;

        default:
            conv_f32.u = u32;
            v = conv_f32.f;
        }

        break;

    default:
        /* NJS_OBJ_TYPE_FLOAT64_ARRAY. */

        conv_f64.u = njs_get_u64(u8);

        if (swap) {
            conv_f64.u = njs_bswap_u64(conv_f64.u);
        }

        v = conv_f64.f;
    }

    njs_set_number(&vm->retval, v);

    return NJS_OK;
}


static njs_int_t
njs_data_view_prototype_set(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t type)
{
    double              v;
    uint8_t             *u8;
    uint32_t            u32;
    uint64_t            index;
    njs_int_t           ret;
    njs_bool_t          swap;
    njs_value_t         *this;
    njs_conv_f32_t      conv_f32;
    njs_conv_f64_t      conv_f64;
    njs_data_view_t     *view;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_data_view(this))) {
        njs_type_error(vm, "this is not a DataView");
        return NJS_ERROR;
    }

    ret = njs_value_to_index(vm, njs_arg(args, nargs, 1), &index);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_number(vm, njs_arg(args, nargs, 2), &v);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    swap = njs_bool(njs_arg(args, nargs, 3));

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    view = njs_data_view(this);
    if (njs_slow_path(njs_is_detached_buffer(view->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    if (njs_typed_array_element_size(type) + index > view->byte_length) {
        njs_range_error(vm, "index %uL is outside the bound of the buffer",
                        index);
        return NJS_ERROR;
    }

    buffer = view->buffer;
    u8 = &buffer->u.u8[index + view->offset];

    switch (type) {
    case NJS_OBJ_TYPE_UINT8_ARRAY:
    case NJS_OBJ_TYPE_INT8_ARRAY:
        *u8 = njs_number_to_int32(v);
        break;

    case NJS_OBJ_TYPE_UINT16_ARRAY:
    case NJS_OBJ_TYPE_INT16_ARRAY:
        u32 = (uint16_t) njs_number_to_int32(v);

        if (swap) {
            u32 = njs_bswap_u16(u32);
        }

        njs_set_u16(u8, u32);
        break;

    case NJS_OBJ_TYPE_UINT32_ARRAY:
    case NJS_OBJ_TYPE_INT32_ARRAY:
        u32 = njs_number_to_int32(v);

        if (swap) {
            u32 = njs_bswap_u32(u32);
        }

        njs_set_u32(u8, u32);
        break;

    case NJS_OBJ_TYPE_FLOAT32_ARRAY:
        conv_f32.f = (float) v;

        if (swap) {
            conv_f32.u = njs_bswap_u32(conv_f32.u);
        }

        njs_set_u32(u8, conv_f32.u);
        break;

    default:
        /* NJS_OBJ_TYPE_FLOAT64_ARRAY. */

        conv_f64.f = v;

        if (swap) {
            conv_f64.u = njs_bswap_u64(conv_f64.u);
        }

        njs_set_u64(u8, conv_f64.u);
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static const njs_object_prop_t  njs_data_view_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("DataView"),
        .configurable = 1,
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
        .name = njs_string("getUint8"),
        .value = njs_native_function2(njs_data_view_prototype_get, 1,
                                      NJS_OBJ_TYPE_UINT8_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getInt8"),
        .value = njs_native_function2(njs_data_view_prototype_get, 1,
                                      NJS_OBJ_TYPE_INT8_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUint16"),
        .value = njs_native_function2(njs_data_view_prototype_get, 1,
                                      NJS_OBJ_TYPE_UINT16_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getInt16"),
        .value = njs_native_function2(njs_data_view_prototype_get, 1,
                                      NJS_OBJ_TYPE_INT16_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getUint32"),
        .value = njs_native_function2(njs_data_view_prototype_get, 1,
                                      NJS_OBJ_TYPE_UINT32_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getInt32"),
        .value = njs_native_function2(njs_data_view_prototype_get, 1,
                                      NJS_OBJ_TYPE_INT32_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getFloat32"),
        .value = njs_native_function2(njs_data_view_prototype_get, 1,
                                      NJS_OBJ_TYPE_FLOAT32_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("getFloat64"),
        .value = njs_native_function2(njs_data_view_prototype_get, 1,
                                      NJS_OBJ_TYPE_FLOAT64_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUint8"),
        .value = njs_native_function2(njs_data_view_prototype_set, 2,
                                      NJS_OBJ_TYPE_UINT8_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setInt8"),
        .value = njs_native_function2(njs_data_view_prototype_set, 2,
                                      NJS_OBJ_TYPE_INT8_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUint16"),
        .value = njs_native_function2(njs_data_view_prototype_set, 2,
                                      NJS_OBJ_TYPE_UINT16_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setInt16"),
        .value = njs_native_function2(njs_data_view_prototype_set, 2,
                                      NJS_OBJ_TYPE_INT16_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setUint32"),
        .value = njs_native_function2(njs_data_view_prototype_set, 2,
                                      NJS_OBJ_TYPE_UINT32_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setInt32"),
        .value = njs_native_function2(njs_data_view_prototype_set, 2,
                                      NJS_OBJ_TYPE_INT32_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setFloat32"),
        .value = njs_native_function2(njs_data_view_prototype_set, 2,
                                      NJS_OBJ_TYPE_FLOAT32_ARRAY),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("setFloat64"),
        .value = njs_native_function2(njs_data_view_prototype_set, 2,
                                      NJS_OBJ_TYPE_FLOAT64_ARRAY),
        .writable = 1,
        .configurable = 1,
    },
};


static const njs_object_init_t  njs_data_view_prototype_init = {
    njs_data_view_prototype_properties,
    njs_nitems(njs_data_view_prototype_properties),
};


const njs_object_type_init_t  njs_data_view_type_init = {
    .constructor = njs_native_ctor(njs_data_view_constructor, 1, 0),
    .prototype_props = &njs_data_view_prototype_init,
    .constructor_props = &njs_data_view_constructor_init,
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
