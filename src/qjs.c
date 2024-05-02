
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>


JSContext *
qjs_new_context(JSRuntime *rt)
{
    JSContext     *ctx;
    qjs_module_t  **module;

    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        return NULL;
    }

    for (module = qjs_modules; *module != NULL; module++) {
        if ((*module)->init(ctx, (*module)->name) == NULL) {
            return NULL;
        }
    }

    return ctx;
}


int
qjs_to_bytes(JSContext *ctx, qjs_bytes_t *bytes, JSValueConst value)
{
    size_t   byte_offset, byte_length;
    JSValue  val;

    val = JS_GetTypedArrayBuffer(ctx, value, &byte_offset, &byte_length, NULL);
    if (!JS_IsException(val)) {
        bytes->start = JS_GetArrayBuffer(ctx, &bytes->length, val);

        JS_FreeValue(ctx, val);

        if (bytes->start != NULL) {
            bytes->tag = JS_TAG_OBJECT;
            bytes->start += byte_offset;
            bytes->length = byte_length;
            return 0;
        }
    }

    bytes->start = JS_GetArrayBuffer(ctx, &bytes->length, value);
    if (bytes->start != NULL) {
        bytes->tag = JS_TAG_OBJECT;
        return 0;
    }

    bytes->tag = JS_TAG_STRING;

    if (!JS_IsString(value)) {
        val = JS_ToString(ctx, value);

        bytes->start = (u_char *) JS_ToCStringLen(ctx, &bytes->length, val);

        JS_FreeValue(ctx, val);

        if (bytes->start == NULL) {
            return -1;
        }
    }

    bytes->start = (u_char *) JS_ToCStringLen(ctx, &bytes->length, value);

    return (bytes->start != NULL) ? 0 : -1;
}


void
qjs_bytes_free(JSContext *ctx, qjs_bytes_t *bytes)
{
    if (bytes->tag == JS_TAG_STRING) {
        JS_FreeCString(ctx, (char *) bytes->start);
    }
}


JSValue
qjs_typed_array_data(JSContext *ctx, JSValueConst value, njs_str_t *data)
{
    size_t  byte_offset, byte_length;

    value = JS_GetTypedArrayBuffer(ctx, value, &byte_offset, &byte_length,
                                   NULL);
    if (JS_IsException(value)) {
        return value;
    }

    data->start = JS_GetArrayBuffer(ctx, &data->length, value);

    JS_FreeValue(ctx, value);

    if (data->start == NULL) {
        return JS_EXCEPTION;
    }

    data->start += byte_offset;
    data->length = byte_length;

    return JS_UNDEFINED;
}
