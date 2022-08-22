
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


#define INT24_MAX  0x7FFFFF
#define INT40_MAX  0x7FFFFFFFFFULL
#define INT48_MAX  0x7FFFFFFFFFFFULL

#define njs_buffer_magic(size, sign, little)                                 \
    ((size << 2) | (sign << 1) | little)


static njs_buffer_encoding_t  njs_buffer_encodings[] =
{
    {
        njs_str("utf-8"),
        njs_string_decode_utf8,
        njs_string_decode_utf8,
        njs_decode_utf8_length
    },

    {
        njs_str("utf8"),
        njs_string_decode_utf8,
        njs_string_decode_utf8,
        njs_decode_utf8_length
    },

    {
        njs_str("hex"),
        njs_string_hex,
        njs_string_decode_hex,
        njs_decode_hex_length
    },

    {
        njs_str("base64"),
        njs_string_base64,
        njs_string_decode_base64,
        njs_decode_base64_length
    },

    {
        njs_str("base64url"),
        njs_string_base64url,
        njs_string_decode_base64url,
        njs_decode_base64url_length
    },

    { njs_null_str, 0, 0, 0 }
};


static njs_int_t njs_buffer_from_object(njs_vm_t *vm, njs_value_t *value);
static njs_int_t njs_buffer_from_array_buffer(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *length, njs_value_t *offset);
static njs_int_t njs_buffer_from_typed_array(njs_vm_t *vm, njs_value_t *value);
static njs_int_t njs_buffer_from_string(njs_vm_t *vm, njs_value_t *value,
    const njs_buffer_encoding_t *encoding);
static njs_int_t njs_buffer_write_string(njs_vm_t *vm, njs_value_t *value,
    njs_typed_array_t *array, const njs_buffer_encoding_t *encoding,
    uint64_t offset, uint64_t length);
static njs_int_t njs_buffer_fill(njs_vm_t *vm, njs_typed_array_t *array,
    const njs_value_t *fill, const njs_value_t *encoding, uint64_t offset,
    uint64_t end);
static njs_int_t njs_buffer_fill_string(njs_vm_t *vm, const njs_value_t *value,
    njs_typed_array_t *array, const njs_buffer_encoding_t *encoding,
    uint8_t *start, uint8_t *end);
static njs_int_t njs_buffer_fill_typed_array(njs_vm_t *vm,
    const njs_value_t *value, njs_typed_array_t *array, uint8_t *start,
    uint8_t *end);

static njs_int_t njs_buffer(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_buffer_constants(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_buffer_constant(njs_vm_t *vm,
    njs_object_prop_t *prop, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);

static njs_int_t njs_buffer_init(njs_vm_t *vm);


static njs_external_t  njs_ext_buffer[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("Buffer"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_buffer,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("constants"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_buffer_constants,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("kMaxLength"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_buffer_constant,
            .magic32 = INT32_MAX,
        }
    },
};


njs_module_t  njs_buffer_module = {
    .name = njs_str("buffer"),
    .init = njs_buffer_init,
};


njs_int_t
njs_buffer_set(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size)
{
    njs_object_t        *proto;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    array = njs_mp_alloc(vm->mem_pool, sizeof(njs_typed_array_t)
                                       + sizeof(njs_array_buffer_t));
    if (njs_slow_path(array == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    buffer = (njs_array_buffer_t *) &array[1];

    proto = &vm->prototypes[NJS_OBJ_TYPE_ARRAY_BUFFER].object;

    njs_lvlhsh_init(&buffer->object.hash);
    njs_lvlhsh_init(&buffer->object.shared_hash);
    buffer->object.__proto__ = proto;
    buffer->object.slots = NULL;
    buffer->object.type = NJS_ARRAY_BUFFER;
    buffer->object.shared = 1;
    buffer->object.extensible = 1;
    buffer->object.error_data = 0;
    buffer->object.fast_array = 0;
    buffer->u.data = (void *) start;
    buffer->size = size;

    proto = &vm->prototypes[NJS_OBJ_TYPE_BUFFER].object;

    array->type = NJS_OBJ_TYPE_UINT8_ARRAY;
    njs_lvlhsh_init(&array->object.hash);
    njs_lvlhsh_init(&array->object.shared_hash);
    array->object.__proto__ = proto;
    array->object.slots = NULL;
    array->object.type = NJS_TYPED_ARRAY;
    array->object.shared = 0;
    array->object.extensible = 1;
    array->object.error_data = 0;
    array->object.fast_array = 1;
    array->buffer = buffer;
    array->offset = 0;
    array->byte_length = size;

    njs_set_typed_array(value, array);

    return NJS_OK;
}


njs_typed_array_t *
njs_buffer_alloc(njs_vm_t *vm, size_t size, njs_bool_t zeroing)
{
    njs_value_t        value;
    njs_typed_array_t  *array;

    njs_set_number(&value, size);

    array = njs_typed_array_alloc(vm, &value, 1, zeroing,
                                  NJS_OBJ_TYPE_UINT8_ARRAY);
    if (njs_slow_path(array == NULL)) {
        return NULL;
    }

    array->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_BUFFER].object;

    return array;
}


njs_int_t
njs_buffer_new(njs_vm_t *vm, njs_value_t *value, const u_char *start,
    uint32_t size)
{
    njs_typed_array_t  *buffer;

    buffer = njs_buffer_alloc(vm, size, 0);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    memcpy(njs_typed_array_buffer(buffer)->u.u8, start, size);

    njs_set_typed_array(value, buffer);

    return NJS_OK;
}


static njs_int_t
njs_buffer_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_type_error(vm, "Buffer is not a constructor");

    return NJS_ERROR;
}


static njs_int_t
njs_buffer_alloc_safe(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t safe)
{
    double             size;
    njs_int_t          ret;
    njs_typed_array_t  *array;
    const njs_value_t  *fill;

    if (njs_slow_path(!njs_is_number(njs_arg(args, nargs, 1)))) {
        njs_type_error(vm, "\"size\" argument must be of type number");
        return NJS_ERROR;
    }

    size = njs_number(njs_argument(args, 1));
    if (njs_slow_path(size < 0 || size > INT32_MAX)) {
        njs_range_error(vm, "invalid size");
        return NJS_ERROR;
    }

    array = njs_buffer_alloc(vm, size, safe || nargs <= 2);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    fill = njs_arg(args, nargs, 2);

    if (safe && njs_is_defined(fill)) {
        ret = njs_buffer_fill(vm, array, fill, njs_arg(args, nargs, 3), 0,
                              array->byte_length);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    njs_set_typed_array(&vm->retval, array);

    return NJS_OK;
}


static njs_int_t
njs_buffer_from(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t                    ret;
    njs_value_t                  *value, retval;
    const njs_buffer_encoding_t  *encoding;

    value = njs_arg(args, nargs, 1);

next:

    switch (value->type) {
    case NJS_TYPED_ARRAY:
        return njs_buffer_from_typed_array(vm, value);

    case NJS_ARRAY_BUFFER:
        return njs_buffer_from_array_buffer(vm, value, njs_arg(args, nargs, 2),
                                            njs_arg(args, nargs, 3));

    case NJS_STRING:
        encoding = njs_buffer_encoding(vm, njs_arg(args, nargs, 2));
        if (njs_slow_path(encoding == NULL)) {
            return NJS_ERROR;
        }

        return njs_buffer_from_string(vm, value, encoding);

    default:
        if (njs_is_object(value)) {
            ret = njs_value_of(vm, value, &retval);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            if (ret == NJS_OK && !njs_is_null(&retval)
                && !(njs_is_object(&retval)
                     && njs_object(&retval) == njs_object(value)))
            {
                *value = retval;
                goto next;
            }

            ret = njs_buffer_from_object(vm, value);
            if (njs_slow_path(ret != NJS_DECLINED)) {
                return ret;
            }
        }

        njs_type_error(vm, "first argument %s is not a string "
                       "or Buffer-like object", njs_type_string(value->type));
    }

    return NJS_ERROR;
}


static njs_int_t
njs_buffer_from_object(njs_vm_t *vm, njs_value_t *value)
{
    double             num;
    int64_t            len;
    uint8_t            *p;
    uint32_t           i;
    njs_str_t          str;
    njs_int_t          ret;
    njs_value_t        data, retval, length;
    njs_typed_array_t  *buffer;

    static const njs_value_t  string_length = njs_string("length");
    static const njs_str_t  str_buffer = njs_str("Buffer");

next:

    ret = njs_value_property(vm, value, njs_value_arg(&string_length),
                             &length);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (ret == NJS_DECLINED) {
        ret = njs_value_property(vm, value, njs_value_arg(&njs_string_type),
                                 &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        ret = njs_value_to_string(vm, &retval, &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        njs_string_get(&retval, &str);

        if (!njs_strstr_eq(&str, &str_buffer)) {
            return NJS_DECLINED;
        }

        ret = njs_value_property(vm, value, njs_value_arg(&njs_string_data),
                                 &retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_is_object(&retval)) {
            njs_value_assign(&data, &retval);
            value = &data;
            goto next;
        }

        return NJS_DECLINED;
    }

    ret = njs_value_to_length(vm, &length, &len);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    buffer = njs_buffer_alloc(vm, len, 0);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    p = njs_typed_array_buffer(buffer)->u.u8;

    for (i = 0; i < len; i++) {
        ret = njs_value_property_i64(vm, value, i, &retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        ret = njs_value_to_number(vm, &retval, &num);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        *p++ = njs_number_to_int32(num);
    }

    njs_set_typed_array(&vm->retval, buffer);

    return NJS_OK;
}


static njs_int_t
njs_buffer_from_array_buffer(njs_vm_t *vm, njs_value_t *value,
    njs_value_t *offset, njs_value_t *length)
{
    int64_t             off, len;
    njs_int_t           ret;
    njs_value_t         arg;
    njs_typed_array_t   *buffer;
    njs_array_buffer_t  *array;

    array = njs_array_buffer(value);

    ret = njs_value_to_index(vm, offset, (uint64_t *) &off);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if ((size_t) off > array->size) {
        njs_range_error(vm, "\"offset\" is outside of buffer bounds");
        return NJS_ERROR;
    }

    if (njs_is_defined(length)) {
        ret = njs_value_to_length(vm, length, &len);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

    } else {
        len = array->size - off;
    }

    if ((size_t) (off + len) > array->size) {
        njs_range_error(vm, "\"length\" is outside of buffer bounds");
        return NJS_ERROR;
    }

    njs_set_array_buffer(&arg, array);

    buffer = njs_typed_array_alloc(vm, &arg, 1, 0, NJS_OBJ_TYPE_UINT8_ARRAY);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    buffer->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_BUFFER].object;

    buffer->offset = off;
    buffer->byte_length = len;

    njs_set_typed_array(&vm->retval, buffer);

    return NJS_OK;
}


static njs_int_t
njs_buffer_from_typed_array(njs_vm_t *vm, njs_value_t *value)
{
    uint8_t            *p;
    uint32_t           i, length;
    njs_typed_array_t  *buffer, *array;

    array = njs_typed_array(value);
    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    length = njs_typed_array_length(array);

    buffer = njs_buffer_alloc(vm, length, 0);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    p = njs_typed_array_buffer(buffer)->u.u8;

    for (i = 0; i < length; i++) {
        *p++ = njs_number_to_int32(njs_typed_array_prop(array, i));
    }

    njs_set_typed_array(&vm->retval, buffer);

    return NJS_OK;
}


static njs_int_t
njs_buffer_from_string(njs_vm_t *vm, njs_value_t *value,
    const njs_buffer_encoding_t *encoding)
{
    njs_int_t          ret;
    njs_str_t          str;
    njs_value_t        dst;
    njs_typed_array_t  *buffer;

    ret = njs_buffer_decode_string(vm, value, &dst, encoding);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_string_get(&dst, &str);

    buffer = njs_buffer_alloc(vm, str.length, 0);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    memcpy(njs_typed_array_buffer(buffer)->u.u8, str.start, str.length);

    njs_set_typed_array(&vm->retval, buffer);

    return NJS_OK;
}


static size_t
njs_buffer_decode_string_length(njs_value_t *value,
    const njs_buffer_encoding_t *encoding)
{
    size_t             size;
    njs_str_t          str;
    njs_string_prop_t  string;

    (void) njs_string_prop(&string, value);

    str.start = string.start;
    str.length = string.size;
    size = string.size;

    if (encoding->decode == njs_string_decode_utf8 && string.length != 0) {
        return size;
    }

    encoding->decode_length(&str, &size);

    return size;
}


static njs_int_t
njs_buffer_byte_length(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t                       size;
    njs_value_t                  *value;
    const njs_buffer_encoding_t  *encoding;

    value = njs_arg(args, nargs, 1);

    switch (value->type) {
    case NJS_TYPED_ARRAY:
        njs_set_number(&vm->retval, njs_typed_array(value)->byte_length);
        return NJS_OK;

    case NJS_ARRAY_BUFFER:
        njs_set_number(&vm->retval, njs_array_buffer(value)->size);
        return NJS_OK;

    case NJS_DATA_VIEW:
        njs_set_number(&vm->retval, njs_data_view(value)->byte_length);
        return NJS_OK;

    case NJS_STRING:
        encoding = njs_buffer_encoding(vm, njs_arg(args, nargs, 2));
        if (njs_slow_path(encoding == NULL)) {
            return NJS_ERROR;
        }

        size = njs_buffer_decode_string_length(value, encoding);

        njs_set_number(&vm->retval, size);

        return NJS_OK;

    default:
        njs_type_error(vm, "first argument %s is not a string "
                       "or Buffer-like object", njs_type_string(value->type));
    }

    return NJS_ERROR;
}


static njs_typed_array_t *
njs_buffer_slot_internal(njs_vm_t *vm, njs_value_t *value)
{
    njs_typed_array_t  *array;

    if (njs_is_object(value)) {
        array = njs_object_proto_lookup(njs_object(value), NJS_TYPED_ARRAY,
                                        njs_typed_array_t);

        if (array != NULL && array->type == NJS_OBJ_TYPE_UINT8_ARRAY) {
            return array;
        }
    }

    return NULL;
}


njs_typed_array_t *
njs_buffer_slot(njs_vm_t *vm, njs_value_t *value, const char *name)
{
    njs_typed_array_t  *array;

    array = njs_buffer_slot_internal(vm, value);
    if (njs_slow_path(array == NULL)) {
        njs_type_error(vm, "\"%s\" argument must be an instance "
                           "of Buffer or Uint8Array", name);
        return NULL;
    }

    return array;
}


static njs_int_t
njs_buffer_array_range(njs_vm_t *vm, njs_typed_array_t *array,
    const njs_value_t *start, const njs_value_t *end, const char *name,
    uint8_t **out_start, uint8_t **out_end)
{
    uint64_t            num_start, num_end;
    njs_int_t           ret;
    njs_array_buffer_t  *buffer;

    num_start = 0;

    if (njs_is_defined(start)) {
        ret = njs_value_to_index(vm, njs_value_arg(start), &num_start);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (num_start > array->byte_length) {
        njs_range_error(vm, "\"%sStart\" is out of range: %L", name, num_start);
        return NJS_ERROR;
    }

    num_end = array->byte_length;

    if (njs_is_defined(end)) {
        ret = njs_value_to_index(vm, njs_value_arg(end), &num_end);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (num_end > array->byte_length) {
        njs_range_error(vm, "\"%sEnd\" is out of range: %L", name, num_end);
        return NJS_ERROR;
    }

    if (num_start > num_end) {
        num_end = num_start;
    }

    buffer = njs_typed_array_buffer(array);
    if (njs_slow_path(njs_is_detached_buffer(buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    *out_start = &buffer->u.u8[array->offset + num_start];
    *out_end = &buffer->u.u8[array->offset + num_end];

    return NJS_OK;
}


static njs_int_t
njs_buffer_compare_array(njs_vm_t *vm, njs_value_t *val1, njs_value_t *val2,
    const njs_value_t *target_start, const njs_value_t *target_end,
    const njs_value_t *source_start, const njs_value_t *source_end)
{
    size_t             size, src_size, trg_size;
    uint8_t            *src, *src_end, *trg, *trg_end;
    njs_int_t          ret;
    njs_typed_array_t  *source, *target;

    source = njs_buffer_slot(vm, val1, "source");
    if (njs_slow_path(source == NULL)) {
        return NJS_ERROR;
    }

    target = njs_buffer_slot(vm, val2, "target");
    if (njs_slow_path(target == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_buffer_array_range(vm, target, target_start, target_end, "target",
                                 &trg, &trg_end);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_buffer_array_range(vm, source, source_start, source_end, "source",
                                 &src, &src_end);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    trg_size = trg_end - trg;
    src_size = src_end - src;

    size = njs_min(trg_size, src_size);

    ret = memcmp(trg, src, size);

    if (ret != 0) {
        njs_set_number(&vm->retval, (ret < 0) ? 1 : -1);
        return NJS_OK;
    }

    if (trg_size > src_size) {
        ret = -1;

    } else if (trg_size < src_size) {
        ret = 1;
    }

    njs_set_number(&vm->retval, ret);

    return NJS_OK;
}


static njs_int_t
njs_buffer_compare(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return njs_buffer_compare_array(vm, njs_arg(args, nargs, 1),
                                    njs_arg(args, nargs, 2),
                                    &njs_value_undefined, &njs_value_undefined,
                                    &njs_value_undefined, &njs_value_undefined);
}


static njs_int_t
njs_buffer_concat(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char             *p, *src;
    size_t             n;
    int64_t            i, len, list_len;
    njs_int_t          ret;
    njs_value_t        *list, *value, *length, retval;
    njs_array_t        *array;
    njs_typed_array_t  *buffer, *arr;

    list = njs_arg(args, nargs, 1);

    if (njs_slow_path(!njs_is_array(list))) {
        njs_type_error(vm, "\"list\" argument must be an instance of Array");
        return NJS_ERROR;
    }

    len = 0;
    ret = njs_object_length(vm, list, &list_len);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    if (njs_is_fast_array(list)) {
        array = njs_array(list);
        for (i = 0; i < list_len; i++) {
            value = &array->start[i];

            if (njs_slow_path(!njs_is_typed_array_uint8(value))) {
                njs_type_error(vm, "\"list[%L]\" argument must be an "
                                   "instance of Buffer or Uint8Array", i);
                return NJS_ERROR;
            }

            arr = njs_typed_array(value);
            if (njs_slow_path(njs_is_detached_buffer(arr->buffer))) {
                njs_type_error(vm, "detached buffer");
                return NJS_ERROR;
            }

            if (njs_slow_path((SIZE_MAX - len) < arr->byte_length)) {
                njs_type_error(vm, "Invalid length");
                return NJS_ERROR;
            }

            len += arr->byte_length;
        }

    } else {

        for (i = 0; i < list_len; i++) {
            ret = njs_value_property_i64(vm, list, i, &retval);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            if (njs_slow_path(!njs_is_typed_array(&retval))) {
                njs_type_error(vm, "\"list[%L]\" argument must be an "
                                   "instance of Buffer or Uint8Array", i);
                return NJS_ERROR;
            }

            arr = njs_typed_array(&retval);
            if (njs_slow_path(njs_is_detached_buffer(arr->buffer))) {
                njs_type_error(vm, "detached buffer");
                return NJS_ERROR;
            }

            if (njs_slow_path((SIZE_MAX - len) < arr->byte_length)) {
                njs_type_error(vm, "Invalid length");
                return NJS_ERROR;
            }

            len += arr->byte_length;
        }
    }

    length = njs_arg(args, nargs, 2);
    if (njs_is_defined(length)) {
        if (njs_slow_path(!njs_is_number(length))) {
            njs_type_error(vm, "\"length\" argument must be of type number");
            return NJS_ERROR;
        }

        len = njs_number(length);
        if (njs_slow_path(len < 0)) {
            njs_range_error(vm, "\"length\" is out of range");
            return NJS_ERROR;
        }
    }

    buffer = njs_buffer_alloc(vm, len, 0);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    p = njs_typed_array_buffer(buffer)->u.u8;

    if (njs_is_fast_array(list)) {
        array = njs_array(list);

        for (i = 0; len != 0 && i < list_len; i++) {
            arr = njs_typed_array(&array->start[i]);
            n = njs_min((size_t) len, arr->byte_length);
            src = &njs_typed_array_buffer(arr)->u.u8[arr->offset];

            p = njs_cpymem(p, src, n);

            len -= n;
        }

    } else {
        for (i = 0; len != 0 && i < list_len; i++) {
            ret = njs_value_property_i64(vm, list, i, &retval);
            if (njs_slow_path(ret == NJS_ERROR)) {
                return ret;
            }

            arr = njs_typed_array(&retval);
            n = njs_min((size_t) len, arr->byte_length);
            src = &njs_typed_array_buffer(arr)->u.u8[arr->offset];

            p = njs_cpymem(p, src, n);

            len -= n;
        }
    }

    if (len != 0) {
        njs_memzero(p, len);
    }

    njs_set_typed_array(&vm->retval, buffer);

    return NJS_OK;
}


static njs_int_t
njs_buffer_is_buffer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_bool_t         is;
    njs_typed_array_t  *array;

    is = 0;

    array = njs_buffer_slot(vm, njs_arg(args, nargs, 1), "source");

    if (njs_fast_path(array != NULL && array->object.__proto__
                      == &vm->prototypes[NJS_OBJ_TYPE_BUFFER].object))
    {
        is = 1;
    }

    njs_set_boolean(&vm->retval, is);

    return NJS_OK;
}


static njs_int_t
njs_buffer_is_encoding(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = njs_arg(args, nargs, 1);
    njs_set_boolean(&vm->retval, njs_is_defined(value)
                                 && njs_buffer_encoding(vm, value) != NULL);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_length(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_typed_array_t  *array;

    array = njs_buffer_slot_internal(vm, value);
    if (njs_slow_path(array == NULL)) {
        njs_set_undefined(retval);
        return NJS_DECLINED;
    }

    njs_set_number(retval, array->byte_length);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_read_int(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t magic)
{
    double              v;
    uint32_t            u32;
    uint64_t            u64, index, size;
    njs_int_t           ret;
    njs_bool_t          little, swap, sign;
    njs_value_t         *this, *value;
    const uint8_t       *u8;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    array = njs_buffer_slot(vm, this, "this");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_index(vm, njs_arg(args, nargs, 1), &index);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    size = magic >> 2;

    if (!size) {
        value = njs_arg(args, nargs, 2);
        if (njs_slow_path(!njs_is_number(value))) {
            njs_type_error(vm, "\"byteLength\" is not a number");
            return NJS_ERROR;
        }

        size = (size_t) njs_number(value);
        if (njs_slow_path(size > 6)) {
            njs_type_error(vm, "\"byteLength\" must be <= 6");
            return NJS_ERROR;
        }
    }

    if (njs_slow_path(size + index > array->byte_length)) {
        njs_range_error(vm, "index %uL is outside the bound of the buffer",
                        index);
        return NJS_ERROR;
    }

    sign = (magic >> 1) & 1;
    little = magic & 1;
    swap = little;

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    buffer = array->buffer;
    if (njs_slow_path(njs_is_detached_buffer(buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    u8 = &buffer->u.u8[index + array->offset];

    switch (size) {
    case 1:
        if (sign) {
            v = (int8_t) *u8;

        } else {
            v = *u8;
        }

        break;

    case 2:
        u32 = njs_get_u16(u8);

        if (swap) {
            u32 = njs_bswap_u16(u32);
        }

        if (sign) {
            /* Sign extension. */

            u32 |= (u32 & (INT16_MAX + 1ULL)) * UINT32_MAX;
            v = (int16_t) u32;

        } else {
            v = u32;
        }

        break;

    case 3:
        if (little) {
            u32 = (u8[2] << 16) | (u8[1] << 8) | u8[0];

        } else {
            u32 = (u8[0] << 16) | (u8[1] << 8) | u8[2];
        }

        if (sign) {
            /* Sign extension. */

            u32 |= (u32 & (INT24_MAX + 1ULL)) * UINT32_MAX;
            v = (int32_t) u32;

        } else {
            v = u32;
        }

        break;

    case 4:
        u32 = njs_get_u32(u8);

        if (swap) {
            u32 = njs_bswap_u32(u32);
        }

        if (sign) {
            /* Sign extension. */

            u32 |= (u32 & (INT32_MAX + 1ULL)) * UINT32_MAX;
            v = (int32_t) u32;

        } else {
            v = u32;
        }

        break;

    case 5:
        if (little) {
            u64 = ((uint64_t) u8[4] << 32)
                  | ((uint64_t) u8[3] << 24)
                  | (u8[2] << 16)
                  | (u8[1] << 8)
                  | u8[0];

        } else {
            u64 = ((uint64_t) u8[0] << 32)
                  | ((uint64_t) u8[1] << 24)
                  | (u8[2] << 16)
                  | (u8[3] << 8)
                  | u8[4];
        }

        if (sign) {
            /* Sign extension. */

            u64 |= (u64 & (INT40_MAX + 1ULL)) * UINT64_MAX;
            v = (int64_t) u64;

        } else {
            v = u64;
        }

        break;

    case 6:
    default:
        if (little) {
            u64 = ((uint64_t) u8[5] << 40)
                  | ((uint64_t) u8[4] << 32)
                  |((uint64_t) u8[3] << 24)
                  | (u8[2] << 16)
                  | (u8[1] << 8)
                  | u8[0];

        } else {
            u64 = ((uint64_t) u8[0] << 40)
                  | ((uint64_t) u8[1] << 32)
                  | ((uint64_t) u8[2] << 24)
                  | (u8[3] << 16)
                  | (u8[4] << 8)
                  | u8[5];
        }

        if (sign) {
            /* Sign extension. */

            u64 |= (u64 & (INT48_MAX + 1ULL)) * UINT64_MAX;
            v = (int64_t) u64;

        } else {
            v = u64;
        }

        break;
    }

    njs_set_number(&vm->retval, v);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_read_float(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic)
{
    double              v;
    uint32_t            u32;
    uint64_t            index, size;
    njs_int_t           ret;
    njs_bool_t          little, swap;
    njs_value_t         *this;
    const uint8_t       *u8;
    njs_conv_f32_t      conv_f32;
    njs_conv_f64_t      conv_f64;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    array = njs_buffer_slot(vm, this, "this");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_index(vm, njs_arg(args, nargs, 1), &index);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    size = magic >> 2;

    if (njs_slow_path(size + index > array->byte_length)) {
        njs_range_error(vm, "index %uL is outside the bound of the buffer",
                        index);
        return NJS_ERROR;
    }

    little = magic & 1;
    swap = little;

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    buffer = array->buffer;
    if (njs_slow_path(njs_is_detached_buffer(buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    u8 = &buffer->u.u8[index + array->offset];

    switch (size) {
    case 4:
        u32 = *((uint32_t *) u8);

        if (swap) {
            u32 = njs_bswap_u32(u32);
        }

        conv_f32.u = u32;
        v = conv_f32.f;
        break;

    case 8:
    default:
        conv_f64.u = *((uint64_t *) u8);

        if (swap) {
            conv_f64.u = njs_bswap_u64(conv_f64.u);
        }

        v = conv_f64.f;
    }

    njs_set_number(&vm->retval, v);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_write_int(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic)
{
    uint8_t             *u8;
    int64_t             i64;
    uint32_t            u32;
    uint64_t            index, size;
    njs_int_t           ret;
    njs_bool_t          little, swap;
    njs_value_t         *this, *value;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    array = njs_buffer_slot(vm, this, "this");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &i64);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    index = 0;

    if (nargs > 2) {
        ret = njs_value_to_index(vm, njs_argument(args, 2), &index);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    size = magic >> 2;

    if (!size) {
        value = njs_arg(args, nargs, 3);
        if (njs_slow_path(!njs_is_number(value))) {
            njs_type_error(vm, "\"byteLength\" is not a number");
            return NJS_ERROR;
        }

        size = (size_t) njs_number(value);
        if (njs_slow_path(size > 6)) {
            njs_type_error(vm, "\"byteLength\" must be <= 6");
            return NJS_ERROR;
        }
    }

    if (njs_slow_path(size + index > array->byte_length)) {
        njs_range_error(vm, "index %uL is outside the bound of the buffer",
                        index);
        return NJS_ERROR;
    }

    little = magic & 1;
    swap = little;

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    u8 = &buffer->u.u8[index + array->offset];

    switch (size) {
    case 1:
        *u8 = i64;
        break;

    case 2:
        u32 = (uint16_t) i64;

        if (swap) {
            u32 = njs_bswap_u16(u32);
        }

        njs_set_u16(u8, u32);
        break;

    case 3:
        if (little) {
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64;

        } else {
            u8 += 2;

            *u8-- = i64; i64 >>= 8;
            *u8-- = i64; i64 >>= 8;
            *u8 = i64;
        }

        break;

    case 4:
        u32 = i64;

        if (swap) {
            u32 = njs_bswap_u32(u32);
        }

        njs_set_u32(u8, u32);
        break;

    case 5:
        if (little) {
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64;

        } else {
            u8 += 4;

            *u8-- = i64; i64 >>= 8;
            *u8-- = i64; i64 >>= 8;
            *u8-- = i64; i64 >>= 8;
            *u8-- = i64; i64 >>= 8;
            *u8 = i64;
        }

        break;

    case 6:
    default:
        if (little) {
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64; i64 >>= 8;
            *u8++ = i64;

        } else {
            u8 += 5;

            *u8-- = i64; i64 >>= 8;
            *u8-- = i64; i64 >>= 8;
            *u8-- = i64; i64 >>= 8;
            *u8-- = i64; i64 >>= 8;
            *u8-- = i64; i64 >>= 8;
            *u8 = i64;
        }

        break;
    }

    njs_set_number(&vm->retval, index + size);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_write_float(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t magic)
{
    double              v;
    uint8_t             *u8;
    uint64_t            index, size;
    njs_int_t           ret;
    njs_bool_t          little, swap;
    njs_value_t         *this;
    njs_conv_f32_t      conv_f32;
    njs_conv_f64_t      conv_f64;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    this = njs_argument(args, 0);
    array = njs_buffer_slot(vm, this, "this");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_value_to_number(vm, njs_arg(args, nargs, 1), &v);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    index = 0;

    if (nargs > 2) {
        ret = njs_value_to_index(vm, njs_argument(args, 2), &index);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    size = magic >> 2;

    if (njs_slow_path(size + index > array->byte_length)) {
        njs_range_error(vm, "index %uL is outside the bound of the buffer",
                        index);
        return NJS_ERROR;
    }

    little = magic & 1;
    swap = little;

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    u8 = &buffer->u.u8[index + array->offset];

    switch (size) {
    case 4:
        conv_f32.f = (float) v;

        if (swap) {
            conv_f32.u = njs_bswap_u32(conv_f32.u);
        }

        *((uint32_t *) u8) = conv_f32.u;
        break;

    case 8:
    default:
        conv_f64.f = v;

        if (swap) {
            conv_f64.u = njs_bswap_u64(conv_f64.u);
        }

        *((uint64_t *) u8) = conv_f64.u;
    }

    njs_set_undefined(&vm->retval);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_write(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint64_t                     offset, length;
    njs_int_t                    ret;
    njs_value_t                  *this, *value, *value_offset, *value_length,
                                 *enc;
    njs_typed_array_t            *array;
    njs_array_buffer_t           *buffer;
    const njs_buffer_encoding_t  *encoding;

    this = njs_argument(args, 0);
    array = njs_buffer_slot(vm, this, "this");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 1);
    value_offset = njs_arg(args, nargs, 2);
    value_length = njs_arg(args, nargs, 3);
    enc = njs_arg(args, nargs, 4);

    offset = 0;
    length = array->byte_length;

    if (njs_slow_path(!njs_is_string(value))) {
        njs_type_error(vm, "first argument must be a string");
        return NJS_ERROR;
    }

    if (njs_is_defined(value_offset)) {
        if (njs_is_string(value_offset)) {
            enc = value_offset;
            goto encoding;
        }

        ret = njs_value_to_index(vm, value_offset, &offset);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    if (njs_is_defined(value_length)) {
        if (njs_is_string(value_length)) {
            enc = value_length;
            goto encoding;
        }

        ret = njs_value_to_index(vm, value_length, &length);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

encoding:

    encoding = njs_buffer_encoding(vm, enc);
    if (njs_slow_path(encoding == NULL)) {
        return NJS_ERROR;
    }

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    if (offset >= array->byte_length) {
        njs_range_error(vm, "\"offset\" is out of range");
        return NJS_ERROR;
    }

    return njs_buffer_write_string(vm, value, array, encoding, offset, length);
}


static njs_int_t
njs_buffer_write_string(njs_vm_t *vm, njs_value_t *value,
    njs_typed_array_t *array, const njs_buffer_encoding_t *encoding,
    uint64_t offset, uint64_t length)
{
    uint8_t             *start;
    njs_int_t           ret;
    njs_str_t           str;
    njs_value_t         dst;
    njs_array_buffer_t  *buffer;

    buffer = njs_typed_array_buffer(array);

    ret = njs_buffer_decode_string(vm, value, &dst, encoding);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_string_get(&dst, &str);

    start = &buffer->u.u8[array->offset + offset];

    if (length > array->byte_length - offset) {
        length = array->byte_length - offset;
    }

    if (str.length == 0) {
        length = 0;
        goto done;
    }

    memcpy(start, str.start, length);

done:

    njs_set_number(&vm->retval, length);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_fill(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    uint64_t           offset, end;
    njs_int_t          ret;
    njs_value_t        *this, *value, *value_offset, *value_end,
                       *encode;
    njs_typed_array_t  *array;

    this = njs_argument(args, 0);
    if (njs_slow_path(nargs < 2)) {
        goto done;
    }

    array = njs_buffer_slot(vm, this, "this");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 1);
    value_offset = njs_arg(args, nargs, 2);
    value_end = njs_arg(args, nargs, 3);
    encode = njs_arg(args, nargs, 4);

    offset = 0;
    end = array->byte_length;

    if (njs_is_defined(value_offset)) {
        if (njs_is_string(value) && njs_is_string(value_offset)) {
            encode = value_offset;
            goto fill;
        }

        ret = njs_value_to_index(vm, value_offset, &offset);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (njs_is_defined(value_end)) {
        if (njs_is_string(value) && njs_is_string(value_end)) {
            encode = value_end;
            goto fill;
        }

        ret = njs_value_to_index(vm, value_end, &end);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

fill:

    ret = njs_buffer_fill(vm, array, value, encode, offset, end);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

done:

    njs_vm_retval_set(vm, this);

    return NJS_OK;
}


static njs_int_t
njs_buffer_fill(njs_vm_t *vm, njs_typed_array_t *array, const njs_value_t *fill,
    const njs_value_t *encode, uint64_t offset, uint64_t end)
{
    double                       num;
    uint8_t                      *start, *stop;
    njs_int_t                    ret;
    njs_array_buffer_t           *buffer;
    const njs_buffer_encoding_t  *encoding;

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(offset > array->byte_length)) {
        njs_range_error(vm, "\"offset\" is out of range");
        return NJS_ERROR;
    }

    if (njs_slow_path(end > array->byte_length)) {
        njs_range_error(vm, "\"end\" is out of range");
        return NJS_ERROR;
    }

    if (njs_slow_path(offset >= end)) {
        return NJS_OK;
    }

    start = &buffer->u.u8[array->offset + offset];
    stop = &buffer->u.u8[array->offset + end];

    switch (fill->type) {
    case NJS_STRING:
        encoding = njs_buffer_encoding(vm, encode);
        if (njs_slow_path(encoding == NULL)) {
            return NJS_ERROR;
        }

        return njs_buffer_fill_string(vm, fill, array, encoding, start, stop);

    case NJS_TYPED_ARRAY:
        return njs_buffer_fill_typed_array(vm, fill, array, start, stop);

    default:
        ret = njs_value_to_number(vm, njs_value_arg(fill), &num);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        memset(start, njs_number_to_uint32(num) & 0xff, end - offset);
    }

    return NJS_OK;
}


static njs_int_t
njs_buffer_fill_string(njs_vm_t *vm, const njs_value_t *value,
    njs_typed_array_t *array, const njs_buffer_encoding_t *encoding,
    uint8_t *start, uint8_t *end)
{
    uint64_t     n;
    njs_int_t    ret;
    njs_str_t    str;
    njs_value_t  dst;

    ret = njs_buffer_decode_string(vm, value, &dst, encoding);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_string_get(&dst, &str);

    if (str.length == 0) {
        memset(start, 0, end - start);
        return NJS_OK;
    }

    while (start < end) {
        n = njs_min(str.length, (size_t) (end - start));
        start = njs_cpymem(start, str.start, n);
    }

    return NJS_OK;
}


static njs_int_t
njs_buffer_fill_typed_array(njs_vm_t *vm, const njs_value_t *value,
    njs_typed_array_t *array, uint8_t *to, uint8_t *end)
{
    size_t              byte_length;
    uint8_t             *from;
    uint64_t            n;
    njs_typed_array_t   *arr_from;
    njs_array_buffer_t  *buffer;

    buffer = njs_typed_array_buffer(array);

    arr_from = njs_typed_array(value);
    byte_length = arr_from->byte_length;
    from = &njs_typed_array_buffer(arr_from)->u.u8[arr_from->offset];

    if (njs_typed_array_buffer(arr_from)->u.u8 == buffer->u.u8) {
        while (to < end) {
            n = njs_min(byte_length, (size_t) (end - to));
            memmove(to, from, n);
            to += n;
        }

    } else {
        while (to < end) {
            n = njs_min(byte_length, (size_t) (end - to));
            to = njs_cpymem(to, from, n);
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    uint64_t                     start, end;
    njs_int_t                    ret;
    njs_str_t                    str;
    njs_value_t                  *this, *value_start, *value_end;
    njs_typed_array_t            *array;
    const njs_buffer_encoding_t  *encoding;

    this = njs_argument(args, 0);
    array = njs_buffer_slot(vm, this, "this");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    value_start = njs_arg(args, nargs, 2);
    value_end = njs_arg(args, nargs, 3);

    start = 0;
    end = array->byte_length;

    encoding = njs_buffer_encoding(vm,  njs_arg(args, nargs, 1));
    if (njs_slow_path(encoding == NULL)) {
        return NJS_ERROR;
    }

    if (njs_is_defined(value_start)) {
        ret = njs_value_to_index(vm, value_start, &start);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        start = njs_min(start, array->byte_length);
    }

    if (njs_is_defined(value_end)) {
        ret = njs_value_to_index(vm, value_end, &end);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        end = njs_min(end, array->byte_length);
    }

    if (njs_slow_path(njs_is_detached_buffer(array->buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    str.start = &njs_typed_array_buffer(array)->u.u8[array->offset + start];
    str.length = end - start;

    if (njs_slow_path(str.length == 0)) {
        njs_vm_retval_set(vm, &njs_string_empty);
        return NJS_OK;
    }

    return encoding->encode(vm, &vm->retval, &str);
}


static njs_int_t
njs_buffer_prototype_compare(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    return njs_buffer_compare_array(vm, njs_argument(args, 0),
                               njs_arg(args, nargs, 1), njs_arg(args, nargs, 2),
                               njs_arg(args, nargs, 3), njs_arg(args, nargs, 4),
                               njs_arg(args, nargs, 5));
}


static njs_int_t
njs_buffer_prototype_copy(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    size_t              size;
    uint8_t             *src, *src_end, *trg, *trg_end;
    njs_int_t           ret;
    njs_value_t         *val1, *val2;
    njs_typed_array_t   *source, *target;
    njs_array_buffer_t  *buffer, *array;

    val1 = njs_argument(args, 0);
    val2 = njs_arg(args, nargs, 1);

    source = njs_buffer_slot(vm, val1, "source");
    if (njs_slow_path(source == NULL)) {
        return NJS_ERROR;
    }

    target = njs_buffer_slot(vm, val2, "target");
    if (njs_slow_path(target == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_buffer_array_range(vm, target, njs_arg(args, nargs, 2),
                                 &njs_value_undefined, "target", &trg,
                                 &trg_end);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_buffer_array_range(vm, source, njs_arg(args, nargs, 3),
                                 njs_arg(args, nargs, 4), "source", &src,
                                 &src_end);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    buffer = njs_typed_array_writable(vm, target);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    array = njs_typed_array_buffer(source);

    size = njs_min(trg_end - trg, src_end - src);

    if (buffer->u.data != array->u.data) {
        memcpy(trg, src, size);

    } else {
        memmove(trg, src, size);
    }

    njs_set_number(&vm->retval, size);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_equals(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    ret = njs_buffer_compare_array(vm, njs_argument(args, 0),
                                  njs_arg(args, nargs, 1), &njs_value_undefined,
                                  &njs_value_undefined, &njs_value_undefined,
                                  &njs_value_undefined);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_boolean(&vm->retval, njs_number(&vm->retval) == 0);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_index_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t last)
{
    uint8_t                      byte;
    int64_t                      from, to, increment, length, offset, index, i;
    njs_int_t                    ret;
    njs_str_t                    str;
    njs_value_t                  *this, *value, *value_from, *enc, dst;
    const uint8_t                *u8;
    njs_typed_array_t            *array, *src;
    njs_array_buffer_t           *buffer;
    const njs_buffer_encoding_t  *encoding;

    this = njs_argument(args, 0);
    value = njs_arg(args, nargs, 1);
    value_from = njs_arg(args, nargs, 2);
    enc = njs_arg(args, nargs, 3);

    array = njs_buffer_slot(vm, this, "this");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    index = -1;

    if (njs_slow_path(array->byte_length == 0)) {
        goto done;
    }

    length = array->byte_length;

    if (last) {
        from = length - 1;
        to = -1;
        increment = -1;

    } else {
        from = 0;
        to = length;
        increment = 1;
    }

    if (njs_is_defined(value_from)) {
        if (njs_is_string(value) && njs_is_string(value_from)) {
            enc = value_from;
            goto encoding;
        }

        ret = njs_value_to_integer(vm, value_from, &from);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (last) {
            if (from >= 0) {
                from = njs_min(from, length - 1);

            } else if (from < 0) {
                from += length;
            }

            if (from <= to) {
                goto done;
            }

        } else {
            if (from < 0) {
                from += length;

                if (from < 0) {
                    from = 0;
                }
            }

            if (from >= to) {
                goto done;
            }
        }
    }

encoding:

    encoding = njs_buffer_encoding(vm, enc);
    if (njs_slow_path(encoding == NULL)) {
        return NJS_ERROR;
    }

    buffer = njs_typed_array_buffer(array);
    if (njs_slow_path(njs_is_detached_buffer(buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    u8 = &buffer->u.u8[0];
    offset = array->offset;

    switch (value->type) {
    case NJS_STRING:
    case NJS_TYPED_ARRAY:
        if (njs_is_string(value)) {
            ret = njs_buffer_decode_string(vm, value, &dst, encoding);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }

            njs_string_get(&dst, &str);

        } else {
            src = njs_typed_array(value);
            if (njs_slow_path(src->type != NJS_OBJ_TYPE_UINT8_ARRAY)) {
                goto fail;
            }

            if (njs_slow_path(njs_is_detached_buffer(src->buffer))) {
                njs_type_error(vm, "detached buffer");
                return NJS_ERROR;
            }

            str.start = &src->buffer->u.u8[src->offset];
            str.length = src->byte_length;
        }

        if (njs_slow_path(str.length == 0)) {
            index = (last) ? length : 0;
            goto done;
        }

        if (last) {
            if (from - to < (int64_t) str.length) {
                goto done;
            }

            from -= str.length - 1;

        } else {
            if (to - from < (int64_t) str.length) {
                goto done;
            }

            to -= str.length - 1;
        }

        for (i = from; i != to; i += increment) {
            if (memcmp(&u8[offset + i], str.start, str.length) == 0) {
                index = i;
                goto done;
            }
        }

        break;

    case NJS_NUMBER:
        byte = njs_number_to_uint32(njs_number(value));

        for (i = from; i != to; i += increment) {
            if (u8[offset + i] == byte) {
                index = i;
                goto done;
            }
        }

        break;

    default:
fail:
        njs_type_error(vm, "\"value\" argument %s is not a string "
                       "or Buffer-like object", njs_type_string(value->type));
        return NJS_ERROR;
    }

done:

    njs_set_number(&vm->retval, index);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_includes(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;

    ret = njs_buffer_prototype_index_of(vm, args, nargs, unused);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_set_boolean(&vm->retval, (njs_number(&vm->retval) != -1));

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_slice(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t          ret;
    njs_typed_array_t  *array;

    ret = njs_typed_array_prototype_slice(vm, args, nargs, unused);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    array = njs_typed_array(&vm->retval);
    array->object.__proto__ = &vm->prototypes[NJS_OBJ_TYPE_BUFFER].object;

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_swap(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t size)
{
    uint8_t             *p, *end;
    njs_typed_array_t   *array;
    njs_array_buffer_t  *buffer;

    array = njs_buffer_slot(vm, njs_argument(args, 0), "this");
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    if ((array->byte_length % size) != 0) {
        njs_range_error(vm, "Buffer size must be a multiple of %d-bits",
                        (int) (size << 3));
        return NJS_ERROR;
    }

    buffer = njs_typed_array_writable(vm, array);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    p = &buffer->u.u8[array->offset];
    end = p + array->byte_length;

    switch (size) {
    case 2:
        for (; p < end; p += 2) {
            njs_set_u16(p, njs_bswap_u16(njs_get_u16(p)));
        }

        break;

    case 4:
        for (; p < end; p += 4) {
            njs_set_u32(p, njs_bswap_u32(njs_get_u32(p)));
        }

        break;

    case 8:
    default:
        for (; p < end; p += 8) {
            njs_set_u64(p, njs_bswap_u64(njs_get_u64(p)));
        }
    }

    njs_set_typed_array(&vm->retval, array);

    return NJS_OK;
}


static njs_int_t
njs_buffer_prototype_to_json(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char              *p, *end;
    njs_int_t           ret;
    njs_value_t         *value;
    njs_value_t         object, array;
    njs_array_t         *arr;
    njs_object_t        *obj;
    njs_typed_array_t   *ta;
    njs_array_buffer_t  *buffer;

    static const njs_value_t  string_buffer = njs_string("Buffer");

    ta = njs_buffer_slot(vm, njs_argument(args, 0), "this");
    if (njs_slow_path(ta == NULL)) {
        return NJS_ERROR;
    }

    obj = njs_object_alloc(vm);
    if (njs_slow_path(obj == NULL)) {
        return NJS_ERROR;
    }

    njs_set_object(&object, obj);

    ret = njs_value_property_set(vm, &object, njs_value_arg(&njs_string_type),
                                 njs_value_arg(&string_buffer));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    arr = njs_array_alloc(vm, 1, ta->byte_length, 0);
    if (njs_slow_path(arr == NULL)) {
        return NJS_ERROR;
    }

    buffer = njs_typed_array_buffer(ta);
    if (njs_slow_path(njs_is_detached_buffer(buffer))) {
        njs_type_error(vm, "detached buffer");
        return NJS_ERROR;
    }

    p = &buffer->u.u8[ta->offset];
    end = p + ta->byte_length;
    value = arr->start;

    while (p < end) {
        njs_set_number(value++, *p++);
    }

    njs_set_array(&array, arr);

    ret = njs_value_property_set(vm, &object, njs_value_arg(&njs_string_data),
                                 &array);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_set_object(&vm->retval, obj);

    return NJS_OK;
}


const njs_buffer_encoding_t *
njs_buffer_encoding(njs_vm_t *vm, const njs_value_t *value)
{
    njs_str_t              name;
    njs_buffer_encoding_t  *encoding;

    if (njs_slow_path(!njs_is_string(value))) {
        if (njs_is_defined(value)) {
            njs_type_error(vm, "encoding must be a string");
            return NULL;
        }

        return &njs_buffer_encodings[0];
    }

    njs_string_get(value, &name);

    for (encoding = &njs_buffer_encodings[0];
         encoding->name.length != 0;
         encoding++)
    {
        if (njs_strstr_eq(&name, &encoding->name)) {
            return encoding;
        }
    }

    njs_type_error(vm, "\"%V\" encoding is not supported", &name);

    return NULL;
}


njs_int_t
njs_buffer_decode_string(njs_vm_t *vm, const njs_value_t *value,
    njs_value_t *dst, const njs_buffer_encoding_t *encoding)
{
    njs_int_t          ret;
    njs_str_t          str;
    njs_string_prop_t  string;

    (void) njs_string_prop(&string, value);

    str.start = string.start;
    str.length = string.size;

    *dst = *value;

    if (encoding->decode == njs_string_decode_utf8 && string.length != 0) {
        return NJS_OK;
    }

    ret = encoding->decode(vm, dst, &str);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    return NJS_OK;
}


static const njs_object_prop_t  njs_buffer_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("Buffer"),
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
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("length"),
        .value = njs_prop_handler(njs_buffer_prototype_length),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readInt8"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(1, 1, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readUInt8"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(1, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readInt16LE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(2, 1, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readUInt16LE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(2, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readInt16BE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(2, 1, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readUInt16BE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(2, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readInt32LE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(4, 1, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readUInt32LE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(4, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readInt32BE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(4, 1, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readUInt32BE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 1,
                                      njs_buffer_magic(4, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readIntLE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 2,
                                      njs_buffer_magic(0, 1, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readUIntLE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 2,
                                      njs_buffer_magic(0, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readIntBE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 2,
                                      njs_buffer_magic(0, 1, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readUIntBE"),
        .value = njs_native_function2(njs_buffer_prototype_read_int, 2,
                                      njs_buffer_magic(0, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readFloatLE"),
        .value = njs_native_function2(njs_buffer_prototype_read_float, 1,
                                      njs_buffer_magic(4, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readFloatBE"),
        .value = njs_native_function2(njs_buffer_prototype_read_float, 1,
                                      njs_buffer_magic(4, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readDoubleLE"),
        .value = njs_native_function2(njs_buffer_prototype_read_float, 1,
                                      njs_buffer_magic(8, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("readDoubleBE"),
        .value = njs_native_function2(njs_buffer_prototype_read_float, 1,
                                      njs_buffer_magic(8, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeInt8"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(1, 1, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeUInt8"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(1, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeInt16LE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(2, 1, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeUInt16LE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(2, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeInt16BE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(2, 1, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeUInt16BE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(2, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeInt32LE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(4, 1, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeUInt32LE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(4, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeInt32BE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(4, 1, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeUInt32BE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 1,
                                      njs_buffer_magic(4, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeIntLE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 3,
                                      njs_buffer_magic(0, 1, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeUIntLE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 3,
                                      njs_buffer_magic(0, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeIntBE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 3,
                                      njs_buffer_magic(0, 1, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeUIntBE"),
        .value = njs_native_function2(njs_buffer_prototype_write_int, 3,
                                      njs_buffer_magic(0, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeFloatLE"),
        .value = njs_native_function2(njs_buffer_prototype_write_float, 1,
                                      njs_buffer_magic(4, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeFloatBE"),
        .value = njs_native_function2(njs_buffer_prototype_write_float, 1,
                                      njs_buffer_magic(4, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeDoubleLE"),
        .value = njs_native_function2(njs_buffer_prototype_write_float, 1,
                                      njs_buffer_magic(8, 0, 1)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("writeDoubleBE"),
        .value = njs_native_function2(njs_buffer_prototype_write_float, 1,
                                      njs_buffer_magic(8, 0, 0)),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("write"),
        .value = njs_native_function(njs_buffer_prototype_write, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("fill"),
        .value = njs_native_function(njs_buffer_prototype_fill, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_buffer_prototype_to_string, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("compare"),
        .value = njs_native_function(njs_buffer_prototype_compare, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("copy"),
        .value = njs_native_function(njs_buffer_prototype_copy, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("equals"),
        .value = njs_native_function(njs_buffer_prototype_equals, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("indexOf"),
        .value = njs_native_function2(njs_buffer_prototype_index_of, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("lastIndexOf"),
        .value = njs_native_function2(njs_buffer_prototype_index_of, 1, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("includes"),
        .value = njs_native_function(njs_buffer_prototype_includes, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("subarray"),
        .value = njs_native_function2(njs_buffer_prototype_slice, 2, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("slice"),
        .value = njs_native_function2(njs_buffer_prototype_slice, 2, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("swap16"),
        .value = njs_native_function2(njs_buffer_prototype_swap, 0, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("swap32"),
        .value = njs_native_function2(njs_buffer_prototype_swap, 0, 4),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("swap64"),
        .value = njs_native_function2(njs_buffer_prototype_swap, 0, 8),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toJSON"),
        .value = njs_native_function(njs_buffer_prototype_to_json, 0),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_buffer_prototype_init = {
    njs_buffer_prototype_properties,
    njs_nitems(njs_buffer_prototype_properties),
};


static const njs_object_prop_t  njs_buffer_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Buffer"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 0, 0.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("alloc"),
        .value = njs_native_function2(njs_buffer_alloc_safe, 0, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("allocUnsafe"),
        .value = njs_native_function2(njs_buffer_alloc_safe, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("allocUnsafeSlow"),
        .value = njs_native_function2(njs_buffer_alloc_safe, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("byteLength"),
        .value = njs_native_function(njs_buffer_byte_length, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("compare"),
        .value = njs_native_function(njs_buffer_compare, 2),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("concat"),
        .value = njs_native_function(njs_buffer_concat, 1),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("from"),
        .value = njs_native_function(njs_buffer_from, 3),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isBuffer"),
        .value = njs_native_function(njs_buffer_is_buffer, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("isEncoding"),
        .value = njs_native_function(njs_buffer_is_encoding, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_buffer_constructor_init = {
    njs_buffer_constructor_properties,
    njs_nitems(njs_buffer_constructor_properties),
};


const njs_object_type_init_t  njs_buffer_type_init = {
   .constructor = njs_native_ctor(njs_buffer_constructor, 0, 0),
   .prototype_props = &njs_buffer_prototype_init,
   .constructor_props = &njs_buffer_constructor_init,
   .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static const njs_object_prop_t  njs_buffer_constants_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("MAX_LENGTH"),
        .value = njs_value(NJS_NUMBER, 1, INT32_MAX),
        .enumerable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("MAX_STRING_LENGTH"),
        .value = njs_value(NJS_NUMBER, 1, NJS_STRING_MAX_LENGTH),
        .enumerable = 1,
    },
};


static const njs_object_init_t  njs_buffer_constants_init = {
    njs_buffer_constants_properties,
    njs_nitems(njs_buffer_constants_properties),
};


static njs_int_t
njs_buffer(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *unused, njs_value_t *retval)
{
    return njs_object_prop_init(vm, &njs_buffer_constructor_init, prop, value,
                                retval);
}


static njs_int_t
njs_buffer_constants(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *unused, njs_value_t *retval)
{
    return njs_object_prop_init(vm, &njs_buffer_constants_init, prop, value,
                                retval);
}


static njs_int_t
njs_buffer_constant(njs_vm_t *vm, njs_object_prop_t *prop, njs_value_t *value,
    njs_value_t *unused, njs_value_t *retval)
{
    njs_value_number_set(retval, njs_vm_prop_magic32(prop));

    return NJS_OK;
}


static njs_int_t
njs_buffer_init(njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_mod_t           *module;
    njs_opaque_value_t  value;

    proto_id = njs_vm_external_prototype(vm, njs_ext_buffer,
                                         njs_nitems(njs_ext_buffer));
    if (njs_slow_path(proto_id < 0)) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    module = njs_module_add(vm, &njs_str_value("buffer"));
    if (njs_slow_path(module == NULL)) {
        return NJS_ERROR;
    }

    njs_value_assign(&module->value, &value);
    module->function.native = 1;

    return NJS_OK;
}
