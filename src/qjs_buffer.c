
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>

#define INT24_MAX  0x7FFFFF
#define INT24_MIN  (-0x800000)
#define INT40_MAX  0x7FFFFFFFFFLL
#define INT40_MIN  (-0x8000000000LL)
#define INT48_MAX  0x7FFFFFFFFFFFLL
#define INT48_MIN  (-0x800000000000LL)
#define UINT24_MAX 0xFFFFFFLL
#define UINT40_MAX 0xFFFFFFFFFFLL
#define UINT48_MAX 0xFFFFFFFFFFFFLL

#define qjs_buffer_magic(size, sign, little)                                 \
      ((size << 2) | (sign << 1) | little)

static JSValue qjs_buffer(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv);
static JSValue qjs_buffer_ctor(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv);
static JSValue qjs_bufferobj_alloc(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int ignored);
static JSValue qjs_buffer_byte_length(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_buffer_compare(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_buffer_concat(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_buffer_fill(JSContext *ctx, JSValueConst buffer,
    JSValueConst fill, JSValueConst encode, uint64_t offset, uint64_t end);
static JSValue qjs_buffer_from(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv);
static JSValue qjs_buffer_is_buffer(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_buffer_is_encoding(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_compare(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_copy(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_equals(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_fill(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_includes(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_index_of(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv, int last);
static JSValue qjs_buffer_prototype_read_float(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv, int magic);
static JSValue qjs_buffer_prototype_read_int(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv, int magic);
static JSValue qjs_buffer_prototype_swap(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int size);
static JSValue qjs_buffer_prototype_to_json(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_to_string(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_write(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_write_int(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv, int magic);
static JSValue qjs_buffer_prototype_write_float(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv, int magic);
static JSValue qjs_buffer_from_string(JSContext *ctx, JSValueConst str,
    JSValueConst encoding);
static JSValue qjs_buffer_from_typed_array(JSContext *ctx, JSValueConst obj,
    size_t offset, size_t size, size_t bytes, int float32);
static JSValue qjs_buffer_from_object(JSContext *ctx, JSValueConst obj);
static JSValue qjs_buffer_compare_array(JSContext *ctx, JSValue val1,
    JSValue val2, JSValueConst target_start, JSValueConst target_end,
    JSValueConst source_start, JSValueConst source_end);
static JSModuleDef *qjs_buffer_init(JSContext *ctx, const char *name);


static qjs_buffer_encoding_t  qjs_buffer_encodings[] =
{
    {
        njs_str("utf-8"),
        NULL,
        NULL,
        NULL,
        NULL,
    },

    {
        njs_str("utf8"),
        NULL,
        NULL,
        NULL,
        NULL,
    },

    {
        njs_str("base64"),
        qjs_base64_encode,
        qjs_base64_encode_length,
        qjs_base64_decode,
        qjs_base64_decode_length,
    },

    {
        njs_str("base64url"),
        qjs_base64url_encode,
        qjs_base64_encode_length,
        qjs_base64url_decode,
        qjs_base64url_decode_length,
    },

    {
        njs_str("hex"),
        qjs_hex_encode,
        qjs_hex_encode_length,
        qjs_hex_decode,
        qjs_hex_decode_length,
    },

    { njs_null_str, 0, 0, 0, 0 }
};


static const JSCFunctionListEntry qjs_buffer_constants[] = {
    JS_PROP_INT32_DEF("MAX_LENGTH", INT32_MAX, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_STRING_LENGTH", 0x3fffffff, JS_PROP_ENUMERABLE),
};


static const JSCFunctionListEntry qjs_buffer_export[] = {
    JS_OBJECT_DEF("constants",
                  qjs_buffer_constants,
                  njs_nitems(qjs_buffer_constants),
                  JS_PROP_CONFIGURABLE),
};


static const JSCFunctionListEntry qjs_buffer_props[] = {
    JS_CFUNC_MAGIC_DEF("alloc", 3, qjs_bufferobj_alloc, 0),
    JS_CFUNC_MAGIC_DEF("allocUnsafe", 3, qjs_bufferobj_alloc, 1),
    JS_CFUNC_DEF("byteLength", 2, qjs_buffer_byte_length),
    JS_CFUNC_DEF("compare", 6, qjs_buffer_compare),
    JS_CFUNC_DEF("concat", 2, qjs_buffer_concat),
    JS_CFUNC_DEF("from", 3, qjs_buffer_from),
    JS_CFUNC_DEF("isBuffer", 1, qjs_buffer_is_buffer),
    JS_CFUNC_DEF("isEncoding", 1, qjs_buffer_is_encoding),
};


static const JSCFunctionListEntry qjs_buffer_proto[] = {
    JS_CFUNC_DEF("compare", 5, qjs_buffer_prototype_compare),
    JS_CFUNC_DEF("copy", 5, qjs_buffer_prototype_copy),
    JS_CFUNC_DEF("equals", 1, qjs_buffer_prototype_equals),
    JS_CFUNC_DEF("fill", 4, qjs_buffer_prototype_fill),
    JS_CFUNC_DEF("includes", 3, qjs_buffer_prototype_includes),
    JS_CFUNC_MAGIC_DEF("indexOf", 3, qjs_buffer_prototype_index_of, 0),
    JS_CFUNC_MAGIC_DEF("lastIndexOf", 3, qjs_buffer_prototype_index_of, 1),
    JS_CFUNC_MAGIC_DEF("readFloatLE", 1, qjs_buffer_prototype_read_float,
                       qjs_buffer_magic(4, 1, 1)),
    JS_CFUNC_MAGIC_DEF("readFloatBE", 1, qjs_buffer_prototype_read_float,
                       qjs_buffer_magic(4, 1, 0)),
    JS_CFUNC_MAGIC_DEF("readDoubleLE", 1, qjs_buffer_prototype_read_float,
                       qjs_buffer_magic(8, 1, 1)),
    JS_CFUNC_MAGIC_DEF("readDoubleBE", 1, qjs_buffer_prototype_read_float,
                       qjs_buffer_magic(8, 1, 0)),
    JS_CFUNC_MAGIC_DEF("readInt8", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(1, 1, 1)),
    JS_CFUNC_MAGIC_DEF("readUInt8", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(1, 0, 1)),
    JS_CFUNC_MAGIC_DEF("readInt16LE", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(2, 1, 1)),
    JS_CFUNC_MAGIC_DEF("readUInt16LE", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(2, 0, 1)),
    JS_CFUNC_MAGIC_DEF("readInt16BE", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(2, 1, 0)),
    JS_CFUNC_MAGIC_DEF("readUInt16BE", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(2, 0, 0)),
    JS_CFUNC_MAGIC_DEF("readInt32LE", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(4, 1, 1)),
    JS_CFUNC_MAGIC_DEF("readUInt32LE", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(4, 0, 1)),
    JS_CFUNC_MAGIC_DEF("readInt32BE", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(4, 1, 0)),
    JS_CFUNC_MAGIC_DEF("readUInt32BE", 1, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(4, 0, 0)),
    JS_CFUNC_MAGIC_DEF("readIntLE", 2, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(0, 1, 1)),
    JS_CFUNC_MAGIC_DEF("readUIntLE", 2, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(0, 0, 1)),
    JS_CFUNC_MAGIC_DEF("readIntBE", 2, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(0, 1, 0)),
    JS_CFUNC_MAGIC_DEF("readUIntBE", 2, qjs_buffer_prototype_read_int,
                       qjs_buffer_magic(0, 0, 0)),
    JS_CFUNC_MAGIC_DEF("swap16", 0, qjs_buffer_prototype_swap, 2),
    JS_CFUNC_MAGIC_DEF("swap32", 0, qjs_buffer_prototype_swap, 4),
    JS_CFUNC_MAGIC_DEF("swap64", 0, qjs_buffer_prototype_swap, 8),
    JS_CFUNC_DEF("toJSON", 0, qjs_buffer_prototype_to_json),
    JS_CFUNC_DEF("toString", 1, qjs_buffer_prototype_to_string),
    JS_CFUNC_DEF("write", 4, qjs_buffer_prototype_write),
    JS_CFUNC_MAGIC_DEF("writeInt8", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(1, 1, 1)),
    JS_CFUNC_MAGIC_DEF("writeUInt8", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(1, 0, 1)),
    JS_CFUNC_MAGIC_DEF("writeInt16LE", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(2, 1, 1)),
    JS_CFUNC_MAGIC_DEF("writeUInt16LE", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(2, 0, 1)),
    JS_CFUNC_MAGIC_DEF("writeInt16BE", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(2, 1, 0)),
    JS_CFUNC_MAGIC_DEF("writeUInt16BE", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(2, 0, 0)),
    JS_CFUNC_MAGIC_DEF("writeInt32LE", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(4, 1, 1)),
    JS_CFUNC_MAGIC_DEF("writeUInt32LE", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(4, 0, 1)),
    JS_CFUNC_MAGIC_DEF("writeInt32BE", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(4, 1, 0)),
    JS_CFUNC_MAGIC_DEF("writeUInt32BE", 1, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(4, 0, 0)),
    JS_CFUNC_MAGIC_DEF("writeIntLE", 2, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(0, 1, 1)),
    JS_CFUNC_MAGIC_DEF("writeUIntLE", 2, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(0, 0, 1)),
    JS_CFUNC_MAGIC_DEF("writeIntBE", 2, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(0, 1, 0)),
    JS_CFUNC_MAGIC_DEF("writeUIntBE", 2, qjs_buffer_prototype_write_int,
                       qjs_buffer_magic(0, 0, 0)),
    JS_CFUNC_MAGIC_DEF("writeFloatLE", 2, qjs_buffer_prototype_write_float,
                       qjs_buffer_magic(4, 1, 1)),
    JS_CFUNC_MAGIC_DEF("writeFloatBE", 2, qjs_buffer_prototype_write_float,
                       qjs_buffer_magic(4, 1, 0)),
    JS_CFUNC_MAGIC_DEF("writeDoubleLE", 2, qjs_buffer_prototype_write_float,
                       qjs_buffer_magic(8, 1, 1)),
    JS_CFUNC_MAGIC_DEF("writeDoubleBE", 2, qjs_buffer_prototype_write_float,
                       qjs_buffer_magic(8, 1, 0)),
};


static JSClassDef qjs_buffer_class = {
    "Buffer",
    .finalizer = NULL,
};


#ifndef NJS_HAVE_QUICKJS_NEW_TYPED_ARRAY
static JSClassDef qjs_uint8_array_ctor_class = {
    "Uint8ArrayConstructor",
    .finalizer = NULL,
};

#endif


qjs_module_t  qjs_buffer_module = {
    .name = "buffer",
    .init = qjs_buffer_init,
};


static u_char   qjs_basis64[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
};


static u_char   qjs_basis64url[] = {
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
    77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 63,
    77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
};


static u_char   qjs_basis64_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static u_char   qjs_basis64url_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";


#define qjs_base64_encoded_length(len)       (((len + 2) / 3) * 4)
#define qjs_base64_decoded_length(len, pad)  (((len / 4) * 3) - pad)


static JSValue
qjs_buffer(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JS_ThrowTypeError(ctx, "Buffer() is deprecated. Use the Buffer.alloc() "
                      "or Buffer.from() methods instead.");

    return JS_EXCEPTION;
}


static JSValue
qjs_buffer_ctor(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue  ret, proto;

    ret = qjs_new_uint8_array(ctx, argc, argv);
    if (JS_IsException(ret)) {
        return ret;
    }

    proto = JS_GetClassProto(ctx, QJS_CORE_CLASS_ID_BUFFER);
    JS_SetPrototype(ctx, ret, proto);
    JS_FreeValue(ctx, proto);

    return ret;
}


static JSValue
qjs_bufferobj_alloc(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int ignored)
{
    JSValue   buffer, ret;
    uint32_t  size;

    if (!JS_IsNumber(argv[0])) {
        return JS_ThrowTypeError(ctx, "The \"size\" argument must be of type"
                                 " number");
    }

    if (JS_ToUint32(ctx, &size, argv[0])) {
        return JS_EXCEPTION;
    }

    buffer = qjs_buffer_alloc(ctx, size);
    if (JS_IsException(buffer)) {
        return buffer;
    }

    if (!JS_IsUndefined(argv[1])) {
        ret = qjs_buffer_fill(ctx, buffer, argv[1], argv[2], 0, size);
        if (JS_IsException(ret)) {
            JS_FreeValue(ctx, buffer);
            return ret;
        }
    }

    return buffer;
}


static JSValue
qjs_buffer_byte_length(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    size_t                       size;
    JSValue                      ret;
    njs_str_t                    src;
    const qjs_buffer_encoding_t  *encoding;

    if (JS_GetArrayBuffer(ctx, &size, argv[0]) != NULL) {
        return JS_NewInt32(ctx, size);
    }

    ret = JS_GetTypedArrayBuffer(ctx, argv[0], NULL, &size, NULL);
    if (!JS_IsException(ret)) {
        JS_FreeValue(ctx, ret);
        return JS_NewInt32(ctx, size);
    }

    if (!JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "first argument is not a string "
                                 "or Buffer-like object");
    }

    encoding = qjs_buffer_encoding(ctx, argv[1], 1);
    if (encoding == NULL) {
        return JS_EXCEPTION;
    }

    src.start = (u_char *) JS_ToCStringLen(ctx, &src.length, argv[0]);

    if (encoding->decode_length != NULL) {
        size = encoding->decode_length(ctx, &src);

    } else {
        size = src.length;
    }

    JS_FreeCString(ctx, (char *) src.start);

    return JS_NewInt32(ctx, size);
}


static JSValue
qjs_buffer_compare(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    return qjs_buffer_compare_array(ctx, argv[0], argv[1], argv[2], argv[3],
                                   argv[4], argv[5]);
}


static JSValue
qjs_buffer_concat(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    u_char     *p;
    size_t     n;
    JSValue    list, length, val, ret, buffer;
    uint32_t   i, len, list_len;
    njs_str_t  buf, dst;

    list = argv[0];

    if (!qjs_is_array(ctx, list)) {
        return JS_ThrowTypeError(ctx,
                            "\"list\" argument must be an instance of Array");
    }

    length = JS_GetPropertyStr(ctx, list, "length");
    if (JS_IsException(length)) {
        return JS_EXCEPTION;
    }

    len = 0;
    if (JS_ToUint32(ctx, &list_len, length)) {
        JS_FreeValue(ctx, length);
        return JS_EXCEPTION;
    }

    JS_FreeValue(ctx, length);

    if (JS_IsUndefined(argv[1])) {
        for (i = 0; i < list_len; i++) {
            val = JS_GetPropertyUint32(ctx, list, i);
            if (JS_IsException(val)) {
                return JS_EXCEPTION;
            }

            ret = qjs_typed_array_data(ctx, val, &buf);
            JS_FreeValue(ctx, val);
            if (JS_IsException(ret)) {
                return JS_ThrowTypeError(ctx, "\"list[%d]\" argument must be an"
                                        " instance of Buffer or Uint8Array", i);
            }

            if ((SIZE_MAX - len) < buf.length) {
                return JS_ThrowTypeError(ctx,
                                         "Total size of buffers is too large");
            }

            len += buf.length;
        }

    } else {
        if (JS_ToUint32(ctx, &len, argv[1])) {
            return JS_EXCEPTION;
        }
    }

    buffer = qjs_buffer_alloc(ctx, len);
    if (JS_IsException(buffer)) {
        return JS_EXCEPTION;
    }

    ret = qjs_typed_array_data(ctx, buffer, &dst);
    if (JS_IsException(ret)) {
        JS_FreeValue(ctx, buffer);
        return JS_EXCEPTION;
    }

    p = dst.start;

    for (i = 0; len != 0 && i < list_len; i++) {
        val = JS_GetPropertyUint32(ctx, list, i);
        if (JS_IsException(val)) {
            JS_FreeValue(ctx, buffer);
            return JS_EXCEPTION;
        }

        ret = qjs_typed_array_data(ctx, val, &buf);
        if (JS_IsException(ret)) {
            JS_FreeValue(ctx, buffer);
            JS_FreeValue(ctx, val);
            return JS_EXCEPTION;
        }

        JS_FreeValue(ctx, val);

        n = njs_min((size_t) len, buf.length);
        p = njs_cpymem(p, buf.start, n);

        len -= n;
    }

    if (len != 0) {
        njs_memzero(p, len);
    }

    return buffer;
}


static JSValue
qjs_buffer_fill(JSContext *ctx, JSValueConst buffer, JSValueConst fill,
    JSValueConst encode, uint64_t offset, uint64_t end)
{
    JSValue    ret, fill_buf;
    uint32_t   n;
    njs_str_t  dst, src;

    ret = qjs_typed_array_data(ctx, buffer, &dst);
    if (JS_IsException(ret)) {
        return ret;
    }

    if (end > dst.length) {
        return JS_ThrowRangeError(ctx, "\"end\" is out of range");
    }

    if (offset >= end) {
        return buffer;
    }

    if (JS_IsNumber(fill)) {
        if (JS_ToUint32(ctx, &n, fill)) {
            return JS_EXCEPTION;
        }

        memset(dst.start + offset, n & 0xff, end - offset);
        return buffer;
    }

    fill_buf = JS_UNDEFINED;

    if (JS_IsString(fill)) {
        fill_buf = qjs_buffer_from_string(ctx, fill, encode);
        if (JS_IsException(fill_buf)) {
            return fill_buf;
        }

        fill = fill_buf;
    }

    ret = qjs_typed_array_data(ctx, fill, &src);
    if (JS_IsException(ret)) {
        JS_FreeValue(ctx, fill_buf);
        return ret;
    }

    if (src.length == 0) {
        memset(dst.start + offset, 0, end - offset);
        JS_FreeValue(ctx, fill_buf);
        return buffer;
    }

    if (src.start >= (dst.start + dst.length)
        || dst.start >= (dst.start + dst.length))
    {
        while (offset < end) {
            n = njs_min(src.length, end - offset);
            memcpy(dst.start + offset, src.start, n);
            offset += n;
        }

    } else {
        while (offset < end) {
            n = njs_min(src.length, end - offset);
            memmove(dst.start + offset, src.start, n);
            offset += n;
        }
    }

    JS_FreeValue(ctx, fill_buf);

    return buffer;
}


static JSValue
qjs_buffer_from(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int         float32;
    size_t      off, size, bytes;
    u_char      *buf;
    JSValue     ret, ctor, name, obj, valueOf;
    const char  *str;

    if (JS_IsString(argv[0])) {
        return qjs_buffer_from_string(ctx, argv[0], argv[1]);

    } else if (ret = JS_GetTypedArrayBuffer(ctx, argv[0], &off, &size, &bytes),
               !JS_IsException(ret))
    {
        float32 = 0;

        if (bytes == 4) {
            /*
             * API workaround for JS_GetTypedArrayBuffer()
             * that does not distinguish between Float32Array and Uint32Array.
             */
            ctor = JS_GetPropertyStr(ctx, argv[0], "constructor");
            if (JS_IsException(ctor)) {
                JS_FreeValue(ctx, ret);
                return ctor;
            }

            name = JS_GetPropertyStr(ctx, ctor, "name");
            if (JS_IsException(name)) {
                JS_FreeValue(ctx, ret);
                return name;
            }

            JS_FreeValue(ctx, ctor);
            str = JS_ToCString(ctx, name);

            if (strncmp(str, "Float32Array", 12) == 0) {
                float32 = 1;
            }

            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, name);
        }

        return qjs_buffer_from_typed_array(ctx, ret, off, size, bytes, float32);

    } else if ((buf = JS_GetArrayBuffer(ctx, &size, argv[0])) != NULL) {
        return qjs_buffer_ctor(ctx, JS_UNDEFINED, argc, argv);

    } else if (JS_IsObject(argv[0])) {
        obj = argv[0];
        valueOf = JS_GetPropertyStr(ctx, obj, "valueOf");
        if (JS_IsException(valueOf)) {
            return valueOf;
        }

        if (JS_IsFunction(ctx, valueOf)) {
            ret = JS_Call(ctx, valueOf, obj, 0, NULL);
            JS_FreeValue(ctx, valueOf);
            if (JS_IsException(ret)) {
                return ret;
            }

            if (JS_IsString(ret)) {
                obj = ret;
                ret = qjs_buffer_from_string(ctx, obj, argv[1]);
                JS_FreeValue(ctx, obj);
                return ret;
            }

            if (JS_IsObject(ret)
                && JS_VALUE_GET_PTR(ret) != JS_VALUE_GET_PTR(obj))
            {
                obj = ret;
                ret = qjs_buffer_from_object(ctx, obj);
                JS_FreeValue(ctx, obj);
                return ret;
            }

            JS_FreeValue(ctx, ret);
        }

        return qjs_buffer_from_object(ctx, obj);
    }

    JS_ThrowTypeError(ctx, "first argument is not a string "
                      "or Buffer-like object");
    return JS_EXCEPTION;
}


static JSValue
qjs_buffer_is_buffer(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue proto, buffer_proto, ret;

    proto = JS_GetPrototype(ctx, argv[0]);
    buffer_proto = JS_GetClassProto(ctx, QJS_CORE_CLASS_ID_BUFFER);

    ret = JS_NewBool(ctx, JS_IsObject(argv[0])
                          && qjs_is_same_value(ctx, proto, buffer_proto));

    JS_FreeValue(ctx, buffer_proto);
    JS_FreeValue(ctx, proto);

    return ret;
}


static JSValue
qjs_buffer_is_encoding(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    return JS_NewBool(ctx, qjs_buffer_encoding(ctx, argv[0], 0) != NULL);
}


static JSValue
qjs_buffer_array_range(JSContext *ctx, njs_str_t *array, JSValueConst start,
    JSValueConst end, const char *name)
{
    int64_t  num_start, num_end;

    num_start = 0;

    if (!JS_IsUndefined(start)) {
        if (JS_ToInt64(ctx, &num_start, start)) {
            return JS_EXCEPTION;
        }
    }

    if (num_start < 0 || (size_t) num_start > array->length) {
        return JS_ThrowRangeError(ctx, "\"%sStart\" is out of range: %" PRId64,
                                  name, num_start);
    }

    num_end = array->length;

    if (!JS_IsUndefined(end)) {
        if (JS_ToInt64(ctx, &num_end, end)) {
            return JS_EXCEPTION;
        }
    }

    if (num_end < 0 || (size_t) num_end > array->length) {
        return JS_ThrowRangeError(ctx, "\"%sEnd\" is out of range: %" PRId64,
                                  name, num_end);
    }

    if (num_start > num_end) {
        num_end = num_start;
    }

    array->start += num_start;
    array->length = num_end - num_start;

    return JS_UNDEFINED;
}


static JSValue
qjs_buffer_compare_array(JSContext *ctx, JSValue val1, JSValue val2,
    JSValueConst target_start, JSValueConst target_end,
    JSValueConst source_start, JSValueConst source_end)
{
    int        rc;
    size_t     size;
    JSValue    ret;
    njs_str_t  src, target;

    ret = qjs_typed_array_data(ctx, val1, &src);
    if (JS_IsException(ret)) {
        return ret;
    }

    ret = qjs_typed_array_data(ctx, val2, &target);
    if (JS_IsException(ret)) {
        return ret;
    }

    ret = qjs_buffer_array_range(ctx, &src, source_start, source_end, "source");
    if (JS_IsException(ret)) {
        return ret;
    }

    ret = qjs_buffer_array_range(ctx, &target, target_start, target_end,
                                 "target");
    if (JS_IsException(ret)) {
        return ret;
    }

    size = njs_min(src.length, target.length);

    rc = memcmp(src.start, target.start, size);

    if (rc != 0) {
        return JS_NewInt32(ctx, (rc < 0) ? -1 : 1);
    }

    if (target.length > src.length) {
        rc = -1;

    } else if (target.length < src.length) {
        rc = 1;
    }

    return JS_NewInt32(ctx, rc);
}


static JSValue
qjs_buffer_prototype_compare(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    return qjs_buffer_compare_array(ctx, this_val, argv[0], argv[1], argv[2],
                                   argv[3], argv[4]);
}


static JSValue
qjs_buffer_prototype_copy(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    size_t     size;
    JSValue    ret;
    njs_str_t  src, target;

    ret = qjs_typed_array_data(ctx, this_val, &src);
    if (JS_IsException(ret)) {
        return ret;
    }

    ret = qjs_typed_array_data(ctx, argv[0], &target);
    if (JS_IsException(ret)) {
        return ret;
    }

    ret = qjs_buffer_array_range(ctx, &target, argv[1], JS_UNDEFINED, "target");
    if (JS_IsException(ret)) {
        return ret;
    }

    ret = qjs_buffer_array_range(ctx, &src, argv[2], argv[3], "source");
    if (JS_IsException(ret)) {
        return ret;
    }

    size = njs_min(src.length, target.length);

    if (src.start >= (target.start + size)
        || target.start >= (src.start + size))
    {
        memcpy(target.start, src.start, size);

    } else {
        memmove(target.start, src.start, size);
    }

    return JS_NewInt32(ctx, size);
}


static JSValue
qjs_buffer_prototype_equals(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue  ret;

    ret = qjs_buffer_compare_array(ctx, this_val, argv[0], JS_UNDEFINED,
                                   JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(ret)) {
        return ret;
    }

    return JS_NewBool(ctx, JS_VALUE_GET_INT(ret) == 0);
}


static JSValue
qjs_buffer_prototype_fill(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue    ret, encode;
    uint64_t   offset, end;
    njs_str_t  dst;

    offset = 0;
    encode = argv[3];

    ret = qjs_typed_array_data(ctx, this_val, &dst);
    if (JS_IsException(ret)) {
        return ret;
    }

    end = dst.length;

    if (!JS_IsUndefined(argv[1])) {
        if (JS_IsString(argv[0]) && JS_IsString(argv[1])) {
            encode = argv[1];
            goto fill;
        }

        if (JS_ToIndex(ctx, &offset, argv[1])) {
            return JS_EXCEPTION;
        }
    }

    if (!JS_IsUndefined(argv[2])) {
        if (JS_IsString(argv[0]) && JS_IsString(argv[2])) {
            encode = argv[2];
            goto fill;
        }

        if (JS_ToIndex(ctx, &end, argv[2])) {
            return JS_EXCEPTION;
        }
    }

fill:

    ret = qjs_buffer_fill(ctx, this_val, argv[0], encode, offset, end);
    if (JS_IsException(ret)) {
        return ret;
    }

    JS_DupValue(ctx, ret);

    return ret;
}


static JSValue
qjs_buffer_prototype_includes(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue    ret;

    ret = qjs_buffer_prototype_index_of(ctx, this_val, argc, argv, 0);
    if (JS_IsException(ret)) {
        return ret;
    }

    return JS_NewBool(ctx, JS_VALUE_GET_INT(ret) != -1);
}


static JSValue
qjs_buffer_prototype_index_of(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int last)
{
    JSValue                      ret, buffer, encode, value;
    int64_t                      from, to, increment, length, i;
    uint32_t                     byte;
    njs_str_t                    self, str;
    const qjs_buffer_encoding_t  *encoding;

    ret = qjs_typed_array_data(ctx, this_val, &self);
    if (JS_IsException(ret)) {
        return ret;
    }

    length = self.length;

    if (last) {
        from = length - 1;
        to = -1;
        increment = -1;

    } else {
        from = 0;
        to = length;
        increment = 1;
    }

    encode = argv[2];

    if (!JS_IsUndefined(argv[1])) {
        if (JS_IsString(argv[0]) && JS_IsString(argv[1])) {
            encode = argv[1];
            goto encoding;
        }

        if (JS_ToInt64(ctx, &from, argv[1])) {
            return JS_EXCEPTION;
        }

        if (from >= 0) {
            from = njs_min(from, length);

        } else {
            from = njs_max(0, length + from);
        }
    }

    if (JS_IsNumber(argv[0])) {
        if (JS_ToUint32(ctx, &byte, argv[0])) {
            return JS_EXCEPTION;
        }

        if (last) {
            from = njs_min(from, length - 1);
        }

        for (i = from; i != to; i += increment) {
            if (self.start[i] == (uint8_t) byte) {
                return JS_NewInt32(ctx, i);
            }
        }

        return JS_NewInt32(ctx, -1);
    }

encoding:

    buffer = JS_UNDEFINED;
    value = argv[0];

    if (JS_IsString(value)) {
        encoding = qjs_buffer_encoding(ctx, encode, 1);
        if (encoding == NULL) {
            return JS_EXCEPTION;
        }

        buffer = qjs_buffer_from_string(ctx, value, encode);
        if (JS_IsException(buffer)) {
            return buffer;
        }

        value = buffer;
    }

    ret = qjs_typed_array_data(ctx, value, &str);
    if (JS_IsException(ret)) {
        JS_FreeValue(ctx, buffer);
        return JS_ThrowTypeError(ctx, "\"value\" argument is not a string "
                                "or Buffer-like object");
    }

    if (last) {
        from = njs_min(from, length - (int64_t) str.length);

        if (to > from) {
            goto done;
        }

    } else {
        to -= (int64_t) str.length - 1;

        if (from > to) {
            goto done;
        }
    }

    if (from == to && str.length == 0) {
        JS_FreeValue(ctx, buffer);
        return JS_NewInt32(ctx, 0);
    }

    for (i = from; i != to; i += increment) {
        if (memcmp(&self.start[i], str.start, str.length) == 0) {
            JS_FreeValue(ctx, buffer);
            return JS_NewInt32(ctx, i);
        }
    }

done:

    JS_FreeValue(ctx, buffer);
    return JS_NewInt32(ctx, -1);
}


static JSValue
qjs_buffer_prototype_read_float(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
    double          v;
    JSValue         ret;
    uint32_t        u32;
    uint64_t        u64, index, size;
    njs_str_t       self;
    njs_bool_t      little, swap;
    njs_conv_f32_t  conv_f32;
    njs_conv_f64_t  conv_f64;

    ret = qjs_typed_array_data(ctx, this_val, &self);
    if (JS_IsException(ret)) {
        return ret;
    }

    if (JS_ToIndex(ctx, &index, argv[0])) {
        return JS_EXCEPTION;
    }

    size = magic >> 2;

    if (size + index > self.length) {
        return JS_ThrowRangeError(ctx, "index %" PRIu64 " is outside the bound"
                                  " of the buffer", index);
    }

    little = magic & 1;
    swap = little;

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    switch (size) {
    case 4:
        u32 = *((uint32_t *) &self.start[index]);

        if (swap) {
            u32 = njs_bswap_u32(u32);
        }

        conv_f32.u = u32;
        v = conv_f32.f;
        break;

    case 8:
    default:
        u64 = *((uint64_t *) &self.start[index]);

        if (swap) {
            u64 = njs_bswap_u64(u64);
        }

        conv_f64.u = u64;
        v = conv_f64.f;
        break;
    }

    return JS_NewFloat64(ctx, v);
}


static JSValue
qjs_buffer_prototype_read_int(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
    JSValue     ret;
    uint32_t    u32;
    uint64_t    u64, index, size;
    njs_str_t   self;
    njs_bool_t  little, swap, sign;

    ret = qjs_typed_array_data(ctx, this_val, &self);
    if (JS_IsException(ret)) {
        return ret;
    }

    if (JS_ToIndex(ctx, &index, argv[0])) {
        return JS_EXCEPTION;
    }

    size = magic >> 2;

    if (!size) {
        if (!JS_IsNumber(argv[1])) {
            return JS_ThrowTypeError(ctx, "\"byteLength\" is not a number");
        }

        if (JS_ToIndex(ctx, &size, argv[1])) {
            return JS_EXCEPTION;
        }

        if (size > 6) {
            return JS_ThrowRangeError(ctx, "\"byteLength\" must be <= 6");
        }
    }

    if (size + index > self.length) {
        return JS_ThrowRangeError(ctx, "index %" PRIu64 " is outside the bound"
                                  " of the buffer", index);
    }

    sign = (magic >> 1) & 1;
    little = magic & 1;
    swap = little;

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    switch (size) {
    case 1:
        if (sign) {
            return JS_NewInt32(ctx, (int8_t) self.start[index]);
        }

        return JS_NewUint32(ctx, self.start[index]);

    case 2:
        u32 = njs_get_u16(&self.start[index]);

        if (swap) {
            u32 = njs_bswap_u16(u32);
        }

        if (sign) {
            /* Sign extension. */
            u32 |= (u32 & (INT16_MAX + 1ULL)) * UINT32_MAX;

            return JS_NewInt32(ctx, (int16_t) u32);
        }

        return JS_NewUint32(ctx, u32);

    case 3:

        if (little) {
            u32 = (self.start[index + 2] << 16)
                  | (self.start[index + 1] << 8)
                  | self.start[index];

        } else {
            u32 = (self.start[index] << 16)
                  | (self.start[index + 1] << 8)
                  | self.start[index + 2];
        }

        if (sign) {
            /* Sign extension. */
            u32 |= (u32 & (INT24_MAX + 1ULL)) * UINT32_MAX;

            return JS_NewInt32(ctx, (int32_t) u32);
        }

        return JS_NewUint32(ctx, u32);

    case 4:
        u32 = njs_get_u32(&self.start[index]);

        if (swap) {
            u32 = njs_bswap_u32(u32);
        }

        if (sign) {
            /* Sign extension. */
            u32 |= (u32 & (INT32_MAX + 1ULL)) * UINT32_MAX;

            return JS_NewInt32(ctx, (int32_t) u32);
        }

        return JS_NewUint32(ctx, u32);

    case 5:
        if (little) {
            u64 = ((uint64_t) self.start[index + 4] << 32)
                  | ((uint64_t) self.start[index + 3] << 24)
                  | (self.start[index + 2] << 16)
                  | (self.start[index + 1] << 8)
                  | self.start[index];

        } else {
            u64 = ((uint64_t) self.start[index] << 32)
                  | ((uint64_t) self.start[index + 1] << 24)
                  | (self.start[index + 2] << 16)
                  | (self.start[index + 3] << 8)
                  | self.start[index + 4];
        }

        if (sign) {
            /* Sign extension. */
            u64 |= (u64 & (INT40_MAX + 1ULL)) * UINT64_MAX;

            return JS_NewFloat64(ctx, (int64_t) u64);
        }

        return JS_NewFloat64(ctx, u64);

    case 6:
    default:
        if (little) {
            u64 = ((uint64_t) self.start[index + 5] << 40)
                  | ((uint64_t) self.start[index + 4] << 32)
                  | ((uint64_t) self.start[index + 3] << 24)
                  | (self.start[index + 2] << 16)
                  | (self.start[index + 1] << 8)
                  | self.start[index];

        } else {
            u64 = ((uint64_t) self.start[index] << 40)
                  | ((uint64_t) self.start[index + 1] << 32)
                  | ((uint64_t) self.start[index + 2] << 24)
                  | (self.start[index + 3] << 16)
                  | (self.start[index + 4] << 8)
                  | self.start[index + 5];
        }

        if (sign) {
            /* Sign extension. */
            u64 |= (u64 & (INT48_MAX + 1ULL)) * UINT64_MAX;

            return JS_NewFloat64(ctx, (int64_t) u64);
        }

        return JS_NewFloat64(ctx, u64);
    }
}


static JSValue
qjs_buffer_prototype_to_json(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int         rc;
    JSValue     obj, data, ret;
    njs_str_t   src;
    njs_uint_t  i;

    ret = qjs_typed_array_data(ctx, this_val, &src);
    if (JS_IsException(ret)) {
        return ret;
    }

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        return obj;
    }

    data = JS_NewArray(ctx);
    if (JS_IsException(data)) {
        JS_FreeValue(ctx, obj);
        return data;
    }

    rc = JS_DefinePropertyValueStr(ctx, obj, "type",
                                   JS_NewString(ctx, "Buffer"),
                                   JS_PROP_ENUMERABLE);
    if (rc == -1) {
        JS_FreeValue(ctx, obj);
        JS_FreeValue(ctx, data);
        return ret;
    }

    rc = JS_DefinePropertyValueStr(ctx, obj, "data", data, JS_PROP_ENUMERABLE);
    if (rc == -1) {
        JS_FreeValue(ctx, obj);
        JS_FreeValue(ctx, data);
        return ret;
    }

    for (i = 0; i < src.length; i++) {
        rc = JS_SetPropertyUint32(ctx, data, i, JS_NewInt32(ctx, src.start[i]));
        if (rc == -1) {
            JS_FreeValue(ctx, obj);
            JS_FreeValue(ctx, data);
            return ret;
        }
    }

    return obj;
}


static JSValue
qjs_buffer_prototype_swap(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int size)
{
    uint8_t    *p, *end;
    JSValue    ret;
    njs_str_t  self;

    ret = qjs_typed_array_data(ctx, this_val, &self);
    if (JS_IsException(ret)) {
        return ret;
    }

    if ((self.length % size) != 0) {
        return JS_ThrowRangeError(ctx, "Buffer size must be a multiple "
                                  "of %d-bits", (int) (size << 3));
    }

    p = self.start;
    end = p + self.length;

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

    JS_DupValue(ctx, this_val);

    return this_val;
}


static JSValue
qjs_buffer_prototype_to_string(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue                      ret;
    njs_str_t                    src, data;
    const qjs_buffer_encoding_t  *encoding;

    ret = qjs_typed_array_data(ctx, this_val, &src);
    if (JS_IsException(ret)) {
        return JS_ThrowTypeError(ctx, "method toString() called on incompatible"
                                 " object");
    }

    if (JS_IsUndefined(argv[0]) || src.length == 0) {
        return JS_NewStringLen(ctx, (char *) src.start, src.length);
    }

    encoding = qjs_buffer_encoding(ctx, argv[0], 1);
    if (njs_slow_path(encoding == NULL)) {
        return JS_EXCEPTION;
    }

    if (encoding->encode_length == NULL) {
        return JS_NewStringLen(ctx, (char *) src.start, src.length);
    }

    data.length = encoding->encode_length(ctx, &src);
    data.start = js_malloc(ctx, data.length);
    if (njs_slow_path(data.start == NULL)) {
        JS_ThrowOutOfMemory(ctx);
        return JS_EXCEPTION;
    }

    if (encoding->encode(ctx, &src, &data) != 0) {
        js_free(ctx, data.start);
        JS_ThrowTypeError(ctx, "failed to encode buffer");
        return JS_EXCEPTION;
    }

    ret = JS_NewStringLen(ctx, (char *) data.start, data.length);

    js_free(ctx, data.start);

    return ret;
}


static JSValue
qjs_buffer_prototype_write(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue                      ret, buffer, encode;
    uint64_t                     offset, max_length;
    njs_str_t                    self, src;
    const uint8_t                *p, *end, *prev;
    const qjs_buffer_encoding_t  *encoding;

    ret = qjs_typed_array_data(ctx, this_val, &self);
    if (JS_IsException(ret)) {
        return ret;
    }

    offset = 0;
    max_length = self.length;
    encode = argv[3];

    if (!JS_IsUndefined(argv[1])) {
        if (JS_IsString(argv[0]) && JS_IsString(argv[1])) {
            encode = argv[1];
            goto write;
        }

        if (JS_ToIndex(ctx, &offset, argv[1])) {
            return JS_EXCEPTION;
        }

        max_length = self.length - offset;
    }

    if (!JS_IsUndefined(argv[2])) {
        if (JS_IsString(argv[0]) && JS_IsString(argv[2])) {
            encode = argv[2];
            goto write;
        }

        if (JS_ToIndex(ctx, &max_length, argv[2])) {
            return JS_EXCEPTION;
        }
    }

write:

    encoding = qjs_buffer_encoding(ctx, encode, 1);
    if (encoding == NULL) {
        return JS_EXCEPTION;
    }

    buffer = qjs_buffer_from_string(ctx, argv[0], encode);
    if (JS_IsException(buffer)) {
        return buffer;
    }

    (void) qjs_typed_array_data(ctx, buffer, &src);

    if (offset > self.length) {
        JS_FreeValue(ctx, buffer);
        return JS_ThrowRangeError(ctx, "\"offset\" is out of range");
    }

    if (src.length == 0) {
        JS_FreeValue(ctx, buffer);
        return JS_NewInt32(ctx, 0);
    }

    if (max_length > self.length - offset) {
        JS_FreeValue(ctx, buffer);
        return JS_ThrowRangeError(ctx, "\"length\" is out of range");
    }

    max_length = njs_min(max_length, src.length);

    if (encoding->decode == NULL) {
        /* Avoid writing incomplete UTF-8 characters. */
        p = prev = src.start;
        end = p + max_length;

        while (p < end) {
            p = njs_utf8_next(p, src.start + src.length);
            if (p <= end) {
                prev = p;
            }
        }

        max_length = prev - src.start;
    }

    memcpy(&self.start[offset], src.start, max_length);

    JS_FreeValue(ctx, buffer);

    return JS_NewInt32(ctx, max_length);
}


static JSValue
qjs_buffer_prototype_write_int(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
    JSValue     ret;
    int64_t     i64;
    uint32_t    u32;
    uint64_t    index, size;
    njs_str_t   self;
    njs_bool_t  little, swap, sign;

    ret = qjs_typed_array_data(ctx, this_val, &self);
    if (JS_IsException(ret)) {
        return ret;
    }

    if (JS_ToIndex(ctx, &index, argv[1])) {
        return JS_EXCEPTION;
    }

    size = magic >> 2;

    if (!size) {
        if (JS_ToIndex(ctx, &size, argv[2])) {
            return JS_EXCEPTION;
        }

        if (size > 6) {
            return JS_ThrowRangeError(ctx, "\"byteLength\" must be <= 6");
        }
    }

    if (size + index > self.length) {
        return JS_ThrowRangeError(ctx, "index %" PRIu64 " is outside the bound"
                                  " of the buffer", index);
    }

    little = magic & 1;
    sign = (magic >> 1) & 1;
    swap = little;

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    if (JS_ToInt64(ctx, &i64, argv[0])) {
        return JS_EXCEPTION;
    }

    switch (size) {
    case 1:
        if (sign) {
            if (i64 < INT8_MIN || i64 > INT8_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }

        } else {
            if (i64 < 0 || i64 > UINT8_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }
        }

        self.start[index] = (uint8_t) i64;
        break;

    case 2:
        u32 = (uint16_t) i64;

        if (sign) {
            if (i64 < INT16_MIN || i64 > INT16_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }

        } else {
            if (i64 < 0 || i64 > UINT16_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }
        }

        if (swap) {
            u32 = njs_bswap_u16(u32);
        }

        njs_set_u16(&self.start[index], u32);
        break;

    case 3:
        if (sign) {
            if (i64 < INT24_MIN || i64 > INT24_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }

        } else {
            if (i64 < 0 || i64 > UINT24_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }
        }

        if (little) {
            self.start[index] = i64; i64 >>= 8;
            self.start[index + 1] = i64; i64 >>= 8;
            self.start[index + 2] = i64;

        } else {
            self.start[index + 2] = i64; i64 >>= 8;
            self.start[index + 1] = i64; i64 >>= 8;
            self.start[index] = i64;
        }

        break;

    case 4:
        u32 = i64;

        if (sign) {
            if (i64 < INT32_MIN || i64 > INT32_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }

        } else {
            if (i64 < 0 || i64 > UINT32_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }
        }

        if (swap) {
            u32 = njs_bswap_u32(u32);
        }

        njs_set_u32(&self.start[index], u32);
        break;

    case 5:
        if (sign) {
            if (i64 < INT40_MIN || i64 > INT40_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }

        } else {
            if (i64 < 0 || i64 > UINT40_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }
        }

        if (little) {
            self.start[index] = i64; i64 >>= 8;
            self.start[index + 1] = i64; i64 >>= 8;
            self.start[index + 2] = i64; i64 >>= 8;
            self.start[index + 3] = i64; i64 >>= 8;
            self.start[index + 4] = i64;

        } else {
            self.start[index + 4] = i64; i64 >>= 8;
            self.start[index + 3] = i64; i64 >>= 8;
            self.start[index + 2] = i64; i64 >>= 8;
            self.start[index + 1] = i64; i64 >>= 8;
            self.start[index] = i64;
        }

        break;

    case 6:
    default:
        if (sign) {
            if (i64 < INT48_MIN || i64 > INT48_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }

        } else {
            if (i64 < 0 || i64 > UINT48_MAX) {
                return JS_ThrowRangeError(ctx, "value is outside the range of"
                                          " representable values");
            }
        }

        if (little) {
            self.start[index] = i64; i64 >>= 8;
            self.start[index + 1] = i64; i64 >>= 8;
            self.start[index + 2] = i64; i64 >>= 8;
            self.start[index + 3] = i64; i64 >>= 8;
            self.start[index + 4] = i64; i64 >>= 8;
            self.start[index + 5] = i64;

        } else {
            self.start[index + 5] = i64; i64 >>= 8;
            self.start[index + 4] = i64; i64 >>= 8;
            self.start[index + 3] = i64; i64 >>= 8;
            self.start[index + 2] = i64; i64 >>= 8;
            self.start[index + 1] = i64; i64 >>= 8;
            self.start[index] = i64;
        }

        break;
    }

    return JS_NewInt32(ctx, size + index);
}


static JSValue
qjs_buffer_prototype_write_float(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic)
{
    double          v;
    JSValue         ret;
    uint32_t        u32;
    uint64_t        u64, index, size;
    njs_str_t       self;
    njs_bool_t      little, swap;
    njs_conv_f32_t  conv_f32;
    njs_conv_f64_t  conv_f64;

    ret = qjs_typed_array_data(ctx, this_val, &self);
    if (JS_IsException(ret)) {
        return ret;
    }

    if (JS_ToFloat64(ctx, &v, argv[0])) {
        return JS_EXCEPTION;
    }

    if (JS_ToIndex(ctx, &index, argv[1])) {
        return JS_EXCEPTION;
    }

    size = magic >> 2;

    if (size + index > self.length) {
        return JS_ThrowRangeError(ctx, "index %" PRIu64 " is outside the bound"
                                  " of the buffer", index);
    }

    little = magic & 1;
    swap = little;

#if NJS_HAVE_LITTLE_ENDIAN
    swap = !swap;
#endif

    switch (size) {
    case 4:
        conv_f32.f = (float) v;

        if (swap) {
            conv_f32.u = njs_bswap_u32(conv_f32.u);
        }

        u32 = conv_f32.u;
        memcpy(&self.start[index], &u32, size);
        break;

    case 8:
    default:
        conv_f64.f = v;

        if (swap) {
            conv_f64.u = njs_bswap_u64(conv_f64.u);
        }

        u64 = conv_f64.u;
        memcpy(&self.start[index], &u64, size);
        break;
    }

    return JS_NewInt32(ctx, size + index);
}


static JSValue
qjs_buffer_from_string(JSContext *ctx, JSValueConst str,
    JSValueConst enc)
{
    size_t                       size;
    JSValue                      buffer, ret;
    njs_str_t                    src, dst;
    const qjs_buffer_encoding_t  *encoding;

    if (!JS_IsString(str)) {
        JS_ThrowTypeError(ctx, "first argument is not a string");
        return JS_EXCEPTION;
    }

    encoding = qjs_buffer_encoding(ctx, enc, 1);
    if (njs_slow_path(encoding == NULL)) {
        return JS_EXCEPTION;
    }

    src.start = (u_char *) JS_ToCStringLen(ctx, &src.length, str);

    if (encoding->decode_length != NULL) {
        size = encoding->decode_length(ctx, &src);

    } else {
        size = src.length;
    }

    buffer = qjs_buffer_alloc(ctx, size);
    if (JS_IsException(buffer)) {
        JS_FreeCString(ctx, (char *) src.start);
        return buffer;
    }

    ret = qjs_typed_array_data(ctx, buffer, &dst);
    if (JS_IsException(ret)) {
        JS_FreeCString(ctx, (char *) src.start);
        return ret;
    }

    if (encoding->decode != NULL) {
        if (encoding->decode(ctx, &src, &dst) != 0) {
            JS_FreeCString(ctx, (char *) src.start);
            JS_ThrowTypeError(ctx, "failed to decode string");
            return JS_EXCEPTION;
        }

    } else {
        memcpy(dst.start, src.start, src.length);
    }

    JS_FreeCString(ctx, (char *) src.start);

    return buffer;
}


static JSValue
qjs_buffer_from_typed_array(JSContext *ctx, JSValueConst arr_buf,
    size_t offset, size_t size, size_t bytes, int float32)
{
    float      *f32;
    u_char     *p, *u8;
    size_t     i;
    double     *f64;
    JSValue    buffer, ret;
    uint16_t   *u16;
    uint32_t   *u32;
    njs_str_t  src, dst;

    size = size / bytes;
    buffer = qjs_buffer_alloc(ctx, size);
    if (JS_IsException(buffer)) {
        JS_FreeValue(ctx, arr_buf);
        return buffer;
    }

    ret = qjs_typed_array_data(ctx, buffer, &dst);
    if (JS_IsException(ret)) {
        JS_FreeValue(ctx, arr_buf);
        JS_FreeValue(ctx, buffer);
        return ret;
    }

    src.start = JS_GetArrayBuffer(ctx, &src.length, arr_buf);
    if (src.start == NULL) {
        JS_FreeValue(ctx, arr_buf);
        JS_FreeValue(ctx, buffer);
        return JS_EXCEPTION;
    }

    p = dst.start;

    switch (bytes) {
    case 1:
        u8 = src.start;
        memcpy(p, u8 + offset, size);
        break;

    case 2:
        u16 = (uint16_t *) src.start;

        for (i = 0; i < size; i++) {
            *p++ = u16[offset + i];
        }

        break;

    case 4:
        if (float32) {
            f32 = (float *) src.start;

            for (i = 0; i < size; i++) {
                *p++ = f32[offset + i];
            }

            break;
        }

        u32 = (uint32_t *) src.start;

        for (i = 0; i < size; i++) {
            *p++ = u32[offset + i];
        }

        break;

    case 8:
        f64 = (double *) src.start;

        for (i = 0; i < size; i++) {
            *p++ = f64[offset + i];
        }

        break;
    }

    JS_FreeValue(ctx, arr_buf);

    return buffer;
}


static JSValue
qjs_buffer_from_object(JSContext *ctx, JSValueConst obj)
{
    int         v;
    u_char      *p;
    int64_t     i, len;
    JSValue     buffer, ret;
    njs_str_t   dst;
    const char  *str;

    ret = JS_GetPropertyStr(ctx, obj, "length");
    if (JS_IsException(ret)) {
        return ret;
    }

    if (JS_IsUndefined(ret)) {
        ret = JS_GetPropertyStr(ctx, obj, "type");
        if (JS_IsException(ret)) {
            return ret;
        }

        if (JS_IsString(ret)) {
            str = JS_ToCString(ctx, ret);
            JS_FreeValue(ctx, ret);
            if (str != NULL) {
                if (strcmp(str, "Buffer") != 0) {
                    JS_FreeCString(ctx, str);
                    goto reject;
                }

                JS_FreeCString(ctx, str);

                ret = JS_GetPropertyStr(ctx, obj, "data");
                if (JS_IsException(ret)) {
                    return ret;
                }

                if (JS_IsObject(ret)) {
                    obj = ret;
                    ret = qjs_buffer_from_object(ctx, obj);
                    JS_FreeValue(ctx, obj);
                    return ret;
                }
            }
        }
    }

    if (!JS_IsNumber(ret)) {
        JS_FreeValue(ctx, ret);
reject:
        JS_ThrowTypeError(ctx, "first argument is not a string "
                          "or Buffer-like object");
        return JS_EXCEPTION;
    }

    len = JS_VALUE_GET_INT(ret);

    buffer = qjs_buffer_alloc(ctx, len);
    if (JS_IsException(buffer)) {
        return buffer;
    }

    ret = qjs_typed_array_data(ctx, buffer, &dst);
    if (JS_IsException(ret)) {
        return ret;
    }

    p = dst.start;

    for (i = 0; i < len; i++) {
        ret = JS_GetPropertyUint32(ctx, obj, i);
        if (njs_slow_path(JS_IsException(ret))) {
            return ret;
        }

        if (njs_slow_path(JS_ToInt32(ctx, &v, ret))) {
            return JS_EXCEPTION;
        }

        JS_FreeValue(ctx, ret);

        *p++ = v;
    }

    return buffer;
}


const qjs_buffer_encoding_t *
qjs_buffer_encoding(JSContext *ctx, JSValueConst value, JS_BOOL thrw)
{
    njs_str_t              name;
    qjs_buffer_encoding_t  *encoding;

    if (!JS_IsString(value)){
        if (!JS_IsUndefined(value)) {
            JS_ThrowTypeError(ctx, "encoding must be a string");
            return NULL;
        }

        return &qjs_buffer_encodings[0];
    }

    name.start = (u_char *) JS_ToCStringLen(ctx, &name.length, value);

    for (encoding = &qjs_buffer_encodings[0];
         encoding->name.length != 0;
         encoding++)
    {
        if (njs_strstr_eq(&name, &encoding->name)) {
            JS_FreeCString(ctx, (char *) name.start);
            return encoding;
        }
    }

    JS_FreeCString(ctx, (char *) name.start);

    if (thrw) {
        JS_ThrowTypeError(ctx, "\"%.*s\" encoding is not supported",
                          (int) name.length, name.start);
    }

    return NULL;
}


static void
qjs_base64_encode_core(njs_str_t *dst, const njs_str_t *src,
    const u_char *basis, njs_bool_t padding)
{
   u_char  *d, *s, c0, c1, c2;
   size_t  len;

    len = src->length;
    s = src->start;
    d = dst->start;

    while (len > 2) {
        c0 = s[0];
        c1 = s[1];
        c2 = s[2];

        *d++ = basis[c0 >> 2];
        *d++ = basis[((c0 & 0x03) << 4) | (c1 >> 4)];
        *d++ = basis[((c1 & 0x0f) << 2) | (c2 >> 6)];
        *d++ = basis[c2 & 0x3f];

        s += 3;
        len -= 3;
    }

    if (len > 0) {
        c0 = s[0];
        *d++ = basis[c0 >> 2];

        if (len == 1) {
            *d++ = basis[(c0 & 0x03) << 4];
            if (padding) {
                *d++ = '=';
                *d++ = '=';
            }

        } else {
            c1 = s[1];

            *d++ = basis[((c0 & 0x03) << 4) | (c1 >> 4)];
            *d++ = basis[(c1 & 0x0f) << 2];

            if (padding) {
                *d++ = '=';
            }
        }

    }

    dst->length = d - dst->start;
}


int
qjs_base64_encode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    qjs_base64_encode_core(dst, src, qjs_basis64_enc, 1);

    return 0;
}


size_t
qjs_base64_encode_length(JSContext *ctx, const njs_str_t *src)
{
    return qjs_base64_encoded_length(src->length);
}


static void
qjs_base64_decode_core(njs_str_t *dst, const njs_str_t *src,
    const u_char *basis)
{
    size_t  len;
    u_char  *d, *s;

    s = src->start;
    d = dst->start;

    len = dst->length;

    while (len >= 3) {
        *d++ = (u_char) (basis[s[0]] << 2 | basis[s[1]] >> 4);
        *d++ = (u_char) (basis[s[1]] << 4 | basis[s[2]] >> 2);
        *d++ = (u_char) (basis[s[2]] << 6 | basis[s[3]]);

        s += 4;
        len -= 3;
    }

    if (len >= 1) {
        *d++ = (u_char) (basis[s[0]] << 2 | basis[s[1]] >> 4);
    }

    if (len >= 2) {
        *d++ = (u_char) (basis[s[1]] << 4 | basis[s[2]] >> 2);
    }
}


static size_t
qjs_base64_decode_length_core(const njs_str_t *src, const u_char *basis)
{
    uint    pad;
    size_t  len;

    for (len = 0; len < src->length; len++) {
        if (basis[src->start[len]] == 77) {
            break;
        }
    }

    pad = 0;

    if (len % 4 != 0) {
        pad = 4 - (len % 4);
        len += pad;
    }

    return qjs_base64_decoded_length(len, pad);
}


int
qjs_base64_decode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    qjs_base64_decode_core(dst, src, qjs_basis64);

    return 0;
}


size_t
qjs_base64_decode_length(JSContext *ctx, const njs_str_t *src)
{
    return qjs_base64_decode_length_core(src, qjs_basis64);
}


int
qjs_base64url_encode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    qjs_base64_encode_core(dst, src, qjs_basis64url_enc, 0);

    return 0;
}


int
qjs_base64url_decode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    qjs_base64_decode_core(dst, src, qjs_basis64url);

    return 0;
}


size_t
qjs_base64url_decode_length(JSContext *ctx, const njs_str_t *src)
{
    return qjs_base64_decode_length_core(src, qjs_basis64url);
}


njs_inline njs_int_t
qjs_char_to_hex(u_char c)
{
    c |= 0x20;

    /* Values less than '0' become >= 208. */
    c = c - '0';

    if (c > 9) {
        /* Values less than 'a' become >= 159. */
        c = c - ('a' - '0');

        if (njs_slow_path(c > 5)) {
            return -1;
        }

        c += 10;
    }

    return c;
}


int
qjs_hex_decode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    u_char        *p;
    size_t        len;
    njs_int_t     c;
    njs_uint_t    i, n;
    const u_char  *start;

    n = 0;
    p = dst->start;

    start = src->start;
    len = src->length;

    for (i = 0; i < len; i++) {
        c = qjs_char_to_hex(start[i]);
        if (njs_slow_path(c < 0)) {
            break;
        }

        n = n * 16 + c;

        if ((i & 1) != 0) {
            *p++ = (u_char) n;
            n = 0;
        }
    }

    dst->length -= (dst->start + dst->length) - p;

    return 0;
}


size_t
qjs_hex_decode_length(JSContext *ctx, const njs_str_t *src)
{
    const u_char  *p, *end;

    p = src->start;
    end = p + src->length;

    for (; p < end; p++) {
        if (njs_slow_path(qjs_char_to_hex(*p) < 0)) {
            break;
        }
    }

    return (p - src->start) / 2;
}


int
qjs_hex_encode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    u_char        *p, c;
    size_t        i, len;
    const u_char  *start;

    static const u_char  hex[16] = "0123456789abcdef";

    len = src->length;
    start = src->start;

    p = dst->start;

    for (i = 0; i < len; i++) {
        c = start[i];
        *p++ = hex[c >> 4];
        *p++ = hex[c & 0x0f];
    }

    return 0;
}


size_t
qjs_hex_encode_length(JSContext *ctx, const njs_str_t *src)
{
    return src->length * 2;
}


JSValue
qjs_buffer_alloc(JSContext *ctx, size_t size)
{
    JSValue  ret, proto, value;

    value = JS_NewInt64(ctx, size);

    ret = qjs_new_uint8_array(ctx, 1, &value);
    if (JS_IsException(ret)) {
        return ret;
    }

    proto = JS_GetClassProto(ctx, QJS_CORE_CLASS_ID_BUFFER);
    JS_SetPrototype(ctx, ret, proto);
    JS_FreeValue(ctx, proto);

    return ret;
}



JSValue
qjs_buffer_create(JSContext *ctx, u_char *start, size_t size)
{
    JSValue    buffer, ret;
    njs_str_t  dst;

    buffer = qjs_buffer_alloc(ctx, size);
    if (JS_IsException(buffer)) {
        return buffer;
    }

    ret = qjs_typed_array_data(ctx, buffer, &dst);
    if (JS_IsException(ret)) {
        return ret;
    }

    memcpy(dst.start, start, size);

    return buffer;
}


JSValue
qjs_buffer_chb_alloc(JSContext *ctx, njs_chb_t *chain)
{
    ssize_t      size;
    JSValue      ret;
    qjs_bytes_t  bytes;

    size = njs_chb_size(chain);
    if (njs_slow_path(size < 0)) {
        JS_ThrowOutOfMemory(ctx);
        return JS_EXCEPTION;
    }

    ret = qjs_buffer_alloc(ctx, size);
    if (JS_IsException(ret)) {
        return ret;
    }

    (void) qjs_to_bytes(ctx, &bytes, ret);

    njs_chb_join_to(chain, bytes.start);
    qjs_bytes_free(ctx, &bytes);

    return ret;
}


JSValue
qjs_new_uint8_array(JSContext *ctx, int argc, JSValueConst *argv)
{
    JSValue  ret;

#ifdef NJS_HAVE_QUICKJS_NEW_TYPED_ARRAY
    ret = JS_NewTypedArray(ctx, argc, argv, JS_TYPED_ARRAY_UINT8);
#else
    JSValue ctor;

    ctor = JS_GetClassProto(ctx, QJS_CORE_CLASS_ID_UINT8_ARRAY_CTOR);
    ret = JS_CallConstructor(ctx, ctor, argc, argv);
    JS_FreeValue(ctx, ctor);
#endif

    return ret;
}


static int
qjs_buffer_builtin_init(JSContext *ctx)
{
    int        rc;
    JSAtom     species_atom;
    JSValue    global_obj, buffer, proto, ctor, ta, ta_proto, symbol, species;
    JSClassID  u8_ta_class_id;

    JS_NewClass(JS_GetRuntime(ctx), QJS_CORE_CLASS_ID_BUFFER,
                &qjs_buffer_class);

    global_obj = JS_GetGlobalObject(ctx);

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, qjs_buffer_proto,
                               njs_nitems(qjs_buffer_proto));

    ctor = JS_GetPropertyStr(ctx, global_obj, "Uint8Array");

#ifndef NJS_HAVE_QUICKJS_NEW_TYPED_ARRAY
    /*
     * Workaround for absence of JS_NewTypedArray() in QuickJS.
     * We use JS_SetClassProto()/JS_GetClassProto() as a key-value store
     * for fast value query by class ID without querying the global object.
     */
    JS_NewClass(JS_GetRuntime(ctx), QJS_CORE_CLASS_ID_UINT8_ARRAY_CTOR,
                &qjs_uint8_array_ctor_class);
    JS_SetClassProto(ctx, QJS_CORE_CLASS_ID_UINT8_ARRAY_CTOR,
                     JS_DupValue(ctx, ctor));
#endif

    ta = JS_CallConstructor(ctx, ctor, 0, NULL);
    u8_ta_class_id = JS_GetClassID(ta);
    JS_FreeValue(ctx, ta);
    JS_FreeValue(ctx, ctor);

    ta_proto = JS_GetClassProto(ctx, u8_ta_class_id);
    JS_SetPrototype(ctx, proto, ta_proto);
    JS_FreeValue(ctx, ta_proto);

    JS_SetClassProto(ctx, QJS_CORE_CLASS_ID_BUFFER, proto);

    buffer = JS_NewCFunction2(ctx, qjs_buffer, "Buffer", 3,
                              JS_CFUNC_constructor, 0);
    if (JS_IsException(buffer)) {
        return -1;
    }

    JS_SetConstructor(ctx, buffer, proto);

    JS_SetPropertyFunctionList(ctx, buffer, qjs_buffer_props,
                               njs_nitems(qjs_buffer_props));

    symbol = JS_GetPropertyStr(ctx, global_obj, "Symbol");
    species = JS_GetPropertyStr(ctx, symbol, "species");
    JS_FreeValue(ctx, symbol);
    species_atom = JS_ValueToAtom(ctx, species);
    JS_FreeValue(ctx, species);

    ctor = JS_NewCFunction2(ctx, qjs_buffer_ctor, "Buffer species ctor", 3,
                            JS_CFUNC_constructor, 0);

    JS_SetProperty(ctx, buffer, species_atom, ctor);
    JS_FreeAtom(ctx, species_atom);

    rc = JS_SetPropertyStr(ctx, global_obj, "Buffer", buffer);
    if (rc == -1) {
        return -1;
    }

    JS_FreeValue(ctx, global_obj);

    return 0;
}


static int
qjs_buffer_module_init(JSContext *ctx, JSModuleDef *m)
{
    int      rc;
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return -1;
    }

    JS_SetPropertyFunctionList(ctx, proto, qjs_buffer_export,
                               njs_nitems(qjs_buffer_export));

    rc = JS_SetModuleExport(ctx, m, "default", proto);
    if (rc != 0) {
        return -1;
    }

    return JS_SetModuleExportList(ctx, m, qjs_buffer_export,
                                  njs_nitems(qjs_buffer_export));
}


static JSModuleDef *
qjs_buffer_init(JSContext *ctx, const char *name)
{
    int          rc;
    JSModuleDef  *m;

    qjs_buffer_builtin_init(ctx);

    m = JS_NewCModule(ctx, name, qjs_buffer_module_init);
    if (m == NULL) {
        return NULL;
    }

    if (JS_AddModuleExport(ctx, m, "default") < 0) {
        return NULL;
    }

    rc = JS_AddModuleExportList(ctx, m, qjs_buffer_export,
                                njs_nitems(qjs_buffer_export));
    if (rc != 0) {
        return NULL;
    }

    return m;
}
