
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>

static JSValue qjs_buffer(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv);
static JSValue qjs_buffer_from(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv);
static JSValue qjs_buffer_is_buffer(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_to_json(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_buffer_prototype_to_string(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue qjs_buffer_from_string(JSContext *ctx, JSValueConst str,
    JSValueConst encoding);
static JSValue qjs_buffer_from_typed_array(JSContext *ctx, JSValueConst obj,
    size_t offset, size_t size, size_t bytes, int float32);
static JSValue qjs_buffer_from_array_buffer(JSContext *ctx, u_char *buf,
    size_t size, JSValueConst offset, JSValueConst length);
static JSValue qjs_buffer_from_object(JSContext *ctx, JSValueConst obj);
static int qjs_base64_encode(JSContext *ctx, const njs_str_t *src,
    njs_str_t *dst);
static size_t qjs_base64_encode_length(JSContext *ctx, const njs_str_t *src);
static int qjs_base64_decode(JSContext *ctx, const njs_str_t *src,
    njs_str_t *dst);
static size_t qjs_base64_decode_length(JSContext *ctx, const njs_str_t *src);
static int qjs_base64url_encode(JSContext *ctx, const njs_str_t *src,
    njs_str_t *dst);
static int qjs_base64url_decode(JSContext *ctx, const njs_str_t *src,
    njs_str_t *dst);
static size_t qjs_base64url_decode_length(JSContext *ctx, const njs_str_t *src);
static int qjs_hex_encode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst);
static size_t qjs_hex_encode_length(JSContext *ctx, const njs_str_t *src);
static int qjs_hex_decode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst);
static size_t qjs_hex_decode_length(JSContext *ctx, const njs_str_t *src);
static JSValue qjs_new_uint8_array(JSContext *ctx, size_t size);
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
    JS_CFUNC_DEF("from", 3, qjs_buffer_from),
    JS_CFUNC_DEF("isBuffer", 1, qjs_buffer_is_buffer),
};


static const JSCFunctionListEntry qjs_buffer_proto[] = {
    JS_CFUNC_DEF("toJSON", 0, qjs_buffer_prototype_to_json),
    JS_CFUNC_DEF("toString", 1, qjs_buffer_prototype_to_string),
};


static JSClassDef qjs_buffer_class = {
    "Buffer",
    .finalizer = NULL,
};


static JSClassID qjs_buffer_class_id;

#ifndef NJS_HAVE_QUICKJS_NEW_TYPED_ARRAY
static JSClassDef qjs_uint8_array_ctor_class = {
    "Uint8ArrayConstructor",
    .finalizer = NULL,
};

static JSClassID qjs_uint8_array_ctor_id;
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
        return qjs_buffer_from_array_buffer(ctx, buf, size, argv[1], argv[2]);

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
    buffer_proto = JS_GetClassProto(ctx, qjs_buffer_class_id);

    ret = JS_NewBool(ctx, JS_VALUE_GET_TAG(argv[0]) == JS_TAG_OBJECT &&
                     JS_VALUE_GET_OBJ(buffer_proto) == JS_VALUE_GET_OBJ(proto));

    JS_FreeValue(ctx, buffer_proto);
    JS_FreeValue(ctx, proto);

    return ret;
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
qjs_buffer_prototype_to_string(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue                      ret;
    njs_str_t                    src, data;
    const qjs_buffer_encoding_t  *encoding;

    ret = qjs_typed_array_data(ctx, this_val, &src);
    if (JS_IsException(ret)) {
        return ret;
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
qjs_buffer_from_string(JSContext *ctx, JSValueConst str,
    JSValueConst enc)
{
    size_t                       size;
    JSValue                      buffer, ret;
    njs_str_t                    src, dst;
    const qjs_buffer_encoding_t  *encoding;

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
qjs_buffer_from_array_buffer(JSContext *ctx, u_char *buf, size_t size,
    JSValueConst offset, JSValueConst length)
{
    JSValue    buffer, ret;
    int64_t    len;
    uint64_t   off;
    njs_str_t  dst;

    if (JS_ToIndex(ctx, &off, offset)) {
        return JS_EXCEPTION;
    }

    if ((size_t) off > size) {
        JS_ThrowRangeError(ctx, "\"offset\" is outside of buffer bounds");
        return JS_EXCEPTION;
    }

    if (JS_IsUndefined(length)) {
        len = size - off;

    } else {
        if (JS_ToInt64(ctx, &len, length)) {
            return JS_EXCEPTION;
        }

        if (len < 0) {
            len = 0;
        }

        if ((size_t) (off + len) > size) {
            JS_ThrowRangeError(ctx, "\"length\" is outside of buffer bounds");
            return JS_EXCEPTION;
        }
    }

    buffer = qjs_buffer_alloc(ctx, len);
    if (JS_IsException(buffer)) {
        return buffer;
    }

    ret = qjs_typed_array_data(ctx, buffer, &dst);
    if (JS_IsException(ret)) {
        return ret;
    }

    memcpy(dst.start, buf + off, len);

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
        JS_ThrowTypeError(ctx, "\"%*s\" encoding is not supported",
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


static int
qjs_base64_encode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    qjs_base64_encode_core(dst, src, qjs_basis64_enc, 1);

    return 0;
}


static size_t
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


static int
qjs_base64_decode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    qjs_base64_decode_core(dst, src, qjs_basis64);

    return 0;
}


static size_t
qjs_base64_decode_length(JSContext *ctx, const njs_str_t *src)
{
    return qjs_base64_decode_length_core(src, qjs_basis64);
}


static int
qjs_base64url_encode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    qjs_base64_encode_core(dst, src, qjs_basis64url_enc, 1);

    return 0;
}


static int
qjs_base64url_decode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst)
{
    qjs_base64_decode_core(dst, src, qjs_basis64url);

    return 0;
}


static size_t
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


static int
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


static size_t
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


static int
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


static size_t
qjs_hex_encode_length(JSContext *ctx, const njs_str_t *src)
{
    return src->length * 2;
}


JSValue
qjs_buffer_alloc(JSContext *ctx, size_t size)
{
    JSValue  ret, proto;

    ret = qjs_new_uint8_array(ctx, size);
    if (JS_IsException(ret)) {
        return ret;
    }

    proto = JS_GetClassProto(ctx, qjs_buffer_class_id);
    JS_SetPrototype(ctx, ret, proto);
    JS_FreeValue(ctx, proto);

    return ret;
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


static JSValue
qjs_new_uint8_array(JSContext *ctx, size_t size)
{
    JSValue  ret, value;

    value = JS_NewInt64(ctx, size);

#ifdef NJS_HAVE_QUICKJS_NEW_TYPED_ARRAY
    ret = JS_NewTypedArray(ctx, 1, &value, JS_TYPED_ARRAY_UINT8);
#else
    JSValue ctor;

    ctor = JS_GetClassProto(ctx, qjs_uint8_array_ctor_id);
    ret = JS_CallConstructor(ctx, ctor, 1, &value);
    JS_FreeValue(ctx, ctor);
#endif

    JS_FreeValue(ctx, value);

    return ret;
}


static int
qjs_buffer_builtin_init(JSContext *ctx)
{
    int        rc;
    JSValue    global_obj, buffer, proto, ctor, ta, ta_proto;
    JSClassID  u8_ta_class_id;

    JS_NewClassID(&qjs_buffer_class_id);
    JS_NewClass(JS_GetRuntime(ctx), qjs_buffer_class_id, &qjs_buffer_class);

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, qjs_buffer_proto,
                               njs_nitems(qjs_buffer_proto));

    global_obj = JS_GetGlobalObject(ctx);

    ctor = JS_GetPropertyStr(ctx, global_obj, "Uint8Array");

#ifndef NJS_HAVE_QUICKJS_NEW_TYPED_ARRAY
    /*
     * Workaround for absence of JS_NewTypedArray() in QuickJS.
     * We use JS_SetClassProto()/JS_GetClassProto() as a key-value store
     * for fast value query by class ID without querying the global object.
     */
    JS_NewClassID(&qjs_uint8_array_ctor_id);
    JS_NewClass(JS_GetRuntime(ctx), qjs_uint8_array_ctor_id,
                &qjs_uint8_array_ctor_class);
    JS_SetClassProto(ctx, qjs_uint8_array_ctor_id, JS_DupValue(ctx, ctor));
#endif

    ta = JS_CallConstructor(ctx, ctor, 0, NULL);
    u8_ta_class_id = JS_GetClassID(ta);
    JS_FreeValue(ctx, ta);
    JS_FreeValue(ctx, ctor);

    ta_proto = JS_GetClassProto(ctx, u8_ta_class_id);
    JS_SetPrototype(ctx, proto, ta_proto);
    JS_FreeValue(ctx, ta_proto);

    JS_SetClassProto(ctx, qjs_buffer_class_id, proto);

    buffer = JS_NewCFunction2(ctx, qjs_buffer, "Buffer", 0,
                              JS_CFUNC_generic, 0);
    if (JS_IsException(buffer)) {
        return -1;
    }

    JS_SetPropertyFunctionList(ctx, buffer, qjs_buffer_props,
                               njs_nitems(qjs_buffer_props));

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

    JS_AddModuleExport(ctx, m, "default");
    rc = JS_AddModuleExportList(ctx, m, qjs_buffer_export,
                                njs_nitems(qjs_buffer_export));
    if (rc != 0) {
        return NULL;
    }

    return m;
}
