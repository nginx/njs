
/*
 * Copyright (C) Alexander Borisov
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef enum {
    NJS_ENCODING_UTF8,
} njs_encoding_t;


typedef struct {
    njs_encoding_t        encoding;
    njs_bool_t            fatal;
    njs_bool_t            ignore_bom;

    njs_unicode_decode_t  ctx;
} njs_encoding_decode_t;


typedef struct {
    njs_str_t             name;
    njs_encoding_t        encoding;
} njs_encoding_label_t;


static njs_encoding_label_t  njs_encoding_labels[] =
{
    { njs_str("utf-8"), NJS_ENCODING_UTF8 },
    { njs_str("utf8") , NJS_ENCODING_UTF8 },
    { njs_null_str, 0 }
};


static njs_int_t njs_text_encoder_encode_utf8(njs_vm_t *vm,
    njs_string_prop_t *prop);
static njs_int_t njs_text_decoder_arg_encoding(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_encoding_decode_t *data);
static njs_int_t njs_text_decoder_arg_options(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_encoding_decode_t *data);


static njs_int_t
njs_text_encoder_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_object_value_t  *encoder;

    if (!vm->top_frame->ctor) {
        njs_type_error(vm, "Constructor of TextEncoder requires 'new'");
        return NJS_ERROR;
    }

    encoder = njs_object_value_alloc(vm, NJS_OBJ_TYPE_TEXT_ENCODER, 0, NULL);
    if (njs_slow_path(encoder == NULL)) {
        return NJS_ERROR;
    }

    njs_set_data(&encoder->value, NULL, NJS_DATA_TAG_TEXT_ENCODER);
    njs_set_object_value(&vm->retval, encoder);

    return NJS_OK;
}


static njs_int_t
njs_text_encoder_encode(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char                *dst;
    size_t                size;
    njs_int_t             ret;
    njs_value_t           *this, *input, value;
    const u_char          *start, *end;
    njs_string_prop_t     prop;
    njs_typed_array_t     *array;
    njs_unicode_decode_t  ctx;

    this = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_object_data(this, NJS_DATA_TAG_TEXT_ENCODER))) {
        njs_type_error(vm, "\"this\" is not a TextEncoder");
        return NJS_ERROR;
    }

    start = NULL;
    end = NULL;

    if (nargs > 1) {
        input = njs_argument(args, 1);

        if (njs_slow_path(!njs_is_string(input))) {
            ret = njs_value_to_string(vm, input, input);
            if (njs_slow_path(ret != NJS_OK)) {
                return ret;
            }
        }

        (void) njs_string_prop(&prop, input);

        if (prop.length != 0) {
            return njs_text_encoder_encode_utf8(vm, &prop);
        }

        start = prop.start;
        end = start + prop.size;
    }

    njs_utf8_decode_init(&ctx);

    (void) njs_utf8_stream_length(&ctx, start, end - start, 1, 0, &size);

    njs_set_number(&value, size);

    array = njs_typed_array_alloc(vm, &value, 1, 0, NJS_OBJ_TYPE_UINT8_ARRAY);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    dst = njs_typed_array_buffer(array)->u.u8;
    njs_utf8_decode_init(&ctx);

    (void) njs_utf8_stream_encode(&ctx, start, end, dst, 1, 0);

    njs_set_typed_array(&vm->retval, array);

    return NJS_OK;
}


static njs_int_t
njs_text_encoder_encode_utf8(njs_vm_t *vm, njs_string_prop_t *prop)
{
    njs_value_t        value;
    njs_typed_array_t  *array;

    njs_set_number(&value, prop->size);

    array = njs_typed_array_alloc(vm, &value, 1, 0, NJS_OBJ_TYPE_UINT8_ARRAY);
    if (njs_slow_path(array == NULL)) {
        return NJS_ERROR;
    }

    memcpy(njs_typed_array_buffer(array)->u.u8, prop->start, prop->size);

    njs_set_typed_array(&vm->retval, array);

    return NJS_OK;
}


static njs_int_t
njs_text_encoder_encode_into(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char                *to, *to_end;
    size_t                size;
    uint32_t              cp;
    njs_int_t             ret;
    njs_str_t             str;
    njs_value_t           *this, *input, *dest, retval, read, written;
    const u_char          *start, *end;
    njs_typed_array_t     *array;
    njs_unicode_decode_t  ctx;

    static const njs_value_t  read_str = njs_string("read");
    static const njs_value_t  written_str = njs_string("written");

    this = njs_argument(args, 0);
    input = njs_arg(args, nargs, 1);
    dest = njs_arg(args, nargs, 2);

    if (njs_slow_path(!njs_is_object_data(this, NJS_DATA_TAG_TEXT_ENCODER))) {
        njs_type_error(vm, "\"this\" is not a TextEncoder");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_string(input))) {
        ret = njs_value_to_string(vm, &retval, input);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        input = &retval;
    }

    if (njs_slow_path(!njs_is_typed_array_uint8(dest))) {
        njs_type_error(vm, "The \"destination\" argument must be an instance "
                       "of Uint8Array");
        return NJS_ERROR;
    }

    njs_string_get(input, &str);

    start = str.start;
    end = start + str.length;

    array = njs_typed_array(dest);
    to = njs_typed_array_start(array);
    to_end = to + array->byte_length;

    cp = 0;
    njs_set_number(&read, 0);
    njs_set_number(&written, 0);

    njs_utf8_decode_init(&ctx);

    while (start < end) {
        cp = njs_utf8_decode(&ctx, &start, end);

        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            cp = NJS_UNICODE_REPLACEMENT;
        }

        size = njs_utf8_size(cp);

        if (to + size > to_end) {
            break;
        }

        njs_number(&read) += (cp > 0xFFFF) ? 2 : 1;
        njs_number(&written) += size;

        to = njs_utf8_encode(to, cp);
    }

    return njs_vm_object_alloc(vm, &vm->retval, &read_str, &read,
                               &written_str, &written, NULL);
}


static const njs_object_prop_t  njs_text_encoder_properties[] =
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
        .name = njs_string("encoding"),
        .value = njs_string("utf-8"),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("encode"),
        .value = njs_native_function(njs_text_encoder_encode, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("encodeInto"),
        .value = njs_native_function(njs_text_encoder_encode_into, 2),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_text_encoder_init = {
    njs_text_encoder_properties,
    njs_nitems(njs_text_encoder_properties),
};


static const njs_object_prop_t  njs_text_encoder_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("TextEncoder"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 0, 0.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_text_encoder_constructor_init = {
    njs_text_encoder_constructor_properties,
    njs_nitems(njs_text_encoder_constructor_properties),
};


const njs_object_type_init_t  njs_text_encoder_type_init = {
   .constructor = njs_native_ctor(njs_text_encoder_constructor, 0, 0),
   .prototype_props = &njs_text_encoder_init,
   .constructor_props = &njs_text_encoder_constructor_init,
   .prototype_value = { .object = { .type = NJS_OBJECT } },
};


static njs_int_t
njs_text_decoder_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t              ret;
    njs_object_value_t     *decoder;
    njs_encoding_decode_t  *data;

    if (!vm->top_frame->ctor) {
        njs_type_error(vm, "Constructor of TextDecoder requires 'new'");
        return NJS_ERROR;
    }

    decoder = njs_object_value_alloc(vm, NJS_OBJ_TYPE_TEXT_DECODER,
                                     sizeof(njs_encoding_decode_t), NULL);
    if (njs_slow_path(decoder == NULL)) {
        return NJS_ERROR;
    }

    data = (njs_encoding_decode_t *) ((uint8_t *) decoder
                                      + sizeof(njs_object_value_t));

    ret = njs_text_decoder_arg_encoding(vm, args, nargs, data);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_text_decoder_arg_options(vm, args, nargs, data);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    njs_utf8_decode_init(&data->ctx);

    njs_set_data(&decoder->value, data, NJS_DATA_TAG_TEXT_DECODER);
    njs_set_object_value(&vm->retval, decoder);

    return NJS_OK;
}


static njs_int_t
njs_text_decoder_arg_encoding(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_encoding_decode_t *data)
{
    njs_str_t             str;
    njs_int_t             ret;
    njs_value_t           *value;
    njs_encoding_label_t  *label;

    if (nargs < 2) {
        data->encoding = NJS_ENCODING_UTF8;
        return NJS_OK;
    }

    value = njs_argument(args, 1);

    if (njs_slow_path(!njs_is_string(value))) {
        ret = njs_value_to_string(vm, value, value);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }
    }

    njs_string_get(value, &str);

    for (label = &njs_encoding_labels[0]; label->name.length != 0; label++) {
        if (njs_strstr_eq(&str, &label->name)) {
            data->encoding = label->encoding;
            return NJS_OK;
        }
    }

    njs_range_error(vm, "The \"%V\" encoding is not supported", &str);

    return NJS_ERROR;
}


static njs_int_t
njs_text_decoder_arg_options(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_encoding_decode_t *data)
{
    njs_int_t    ret;
    njs_value_t  retval, *value;

    static const njs_value_t  fatal_str = njs_string("fatal");
    static const njs_value_t  ignore_bom_str = njs_string("ignoreBOM");

    if (nargs < 3) {
        data->fatal = 0;
        data->ignore_bom = 0;

        return NJS_OK;
    }

    value = njs_argument(args, 2);

    if (njs_slow_path(!njs_is_object(value))) {
        njs_type_error(vm, "The \"options\" argument must be of type object");
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, value, njs_value_arg(&fatal_str), &retval);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    data->fatal = njs_bool(&retval);

    ret = njs_value_property(vm, value, njs_value_arg(&ignore_bom_str),
                             &retval);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return ret;
    }

    data->ignore_bom = njs_bool(&retval);

    return NJS_OK;
}


static njs_int_t
njs_text_decoder_encoding(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_encoding_decode_t  *data;

    static const njs_value_t  utf8_str = njs_string("utf-8");

    if (njs_slow_path(!njs_is_object_data(value, NJS_DATA_TAG_TEXT_DECODER))) {
        njs_set_undefined(retval);
        return NJS_DECLINED;
    }

    data = njs_object_data(value);

    switch (data->encoding) {
    case NJS_ENCODING_UTF8:
        *retval = utf8_str;
        break;

    default:
        njs_type_error(vm, "unknown encoding");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static njs_int_t
njs_text_decoder_fatal(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_encoding_decode_t  *data;

    if (njs_slow_path(!njs_is_object_data(value, NJS_DATA_TAG_TEXT_DECODER))) {
        njs_set_undefined(retval);
        return NJS_DECLINED;
    }

    data = njs_object_data(value);

    njs_set_boolean(retval, data->fatal);

    return NJS_OK;
}


static njs_int_t
njs_text_decoder_ignore_bom(njs_vm_t *vm, njs_object_prop_t *prop,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_encoding_decode_t  *data;

    if (njs_slow_path(!njs_is_object_data(value, NJS_DATA_TAG_TEXT_DECODER))) {
        njs_set_undefined(retval);
        return NJS_DECLINED;
    }

    data = njs_object_data(value);

    njs_set_boolean(retval, data->ignore_bom);

    return NJS_OK;
}


static njs_int_t
njs_text_decoder_decode(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char                    *dst;
    size_t                    size;
    ssize_t                   length;
    njs_int_t                 ret;
    njs_bool_t                stream;
    njs_value_t               retval, *this, *value, *options;
    const u_char              *start, *end;
    njs_unicode_decode_t      ctx;
    njs_encoding_decode_t     *data;
    const njs_typed_array_t   *array;
    const njs_array_buffer_t  *buffer;

    static const njs_value_t  stream_str = njs_string("stream");

    start = NULL;
    end = NULL;

    stream = 0;

    this = njs_argument(args, 0);

    if (njs_slow_path(!njs_is_object_data(this, NJS_DATA_TAG_TEXT_DECODER))) {
        njs_type_error(vm, "\"this\" is not a TextDecoder");
        return NJS_ERROR;
    }

    if (njs_fast_path(nargs > 1)) {
        value = njs_argument(args, 1);

        if (njs_is_typed_array(value)) {
            array = njs_typed_array(value);

            start = njs_typed_array_start(array);
            end = start + array->byte_length;

        } else if (njs_is_array_buffer(value)) {
            buffer = njs_array_buffer(value);

            start = buffer->u.u8;
            end = start + buffer->size;

        } else {
            njs_type_error(vm, "The \"input\" argument must be an instance "
                           "of TypedArray");
            return NJS_ERROR;
        }
    }

    if (nargs > 2) {
        options = njs_argument(args, 2);

        if (njs_slow_path(!njs_is_object(options))) {
            njs_type_error(vm, "The \"options\" argument must be "
                           "of type object");
            return NJS_ERROR;
        }

        ret = njs_value_property(vm, options, njs_value_arg(&stream_str),
                                 &retval);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return ret;
        }

        stream = njs_bool(&retval);
    }

    data = njs_object_data(this);

    ctx = data->ctx;

    /* Looking for BOM. */

    if (!data->ignore_bom) {
        start += njs_utf8_bom(start, end);
    }

    length = njs_utf8_stream_length(&ctx, start, end - start, !stream,
                                    data->fatal, &size);
    if (length == -1) {
        njs_type_error(vm, "The encoded data was not valid");
        return NJS_ERROR;
    }

    dst = njs_string_alloc(vm, &vm->retval, size, length);
    if (njs_slow_path(dst == NULL)) {
        return NJS_ERROR;
    }

    (void) njs_utf8_stream_encode(&data->ctx, start, end, dst, !stream, 0);

    if (!stream) {
        njs_utf8_decode_init(&data->ctx);
    }

    return NJS_OK;
}


static const njs_object_prop_t  njs_text_decoder_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("encoding"),
        .value = njs_prop_handler(njs_text_decoder_encoding),
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("fatal"),
        .value = njs_prop_handler(njs_text_decoder_fatal),
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("ignoreBOM"),
        .value = njs_prop_handler(njs_text_decoder_ignore_bom),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("decode"),
        .value = njs_native_function(njs_text_decoder_decode, 0),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_text_decoder_init = {
    njs_text_decoder_properties,
    njs_nitems(njs_text_decoder_properties),
};


static const njs_object_prop_t  njs_text_decoder_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("TextDecoder"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 0, 0.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_text_decoder_constructor_init = {
    njs_text_decoder_constructor_properties,
    njs_nitems(njs_text_decoder_constructor_properties),
};


const njs_object_type_init_t  njs_text_decoder_type_init = {
   .constructor = njs_native_ctor(njs_text_decoder_constructor, 0, 0),
   .prototype_props = &njs_text_decoder_init,
   .constructor_props = &njs_text_decoder_constructor_init,
   .prototype_value = { .object = { .type = NJS_OBJECT } },
};
