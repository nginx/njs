
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>
#include <njs.h> /* NJS_VERSION */


static JSValue qjs_njs_getter(JSContext *ctx, JSValueConst this_val);


static const JSCFunctionListEntry qjs_global_proto[] = {
    JS_CGETSET_DEF("njs", qjs_njs_getter, NULL),
};


JSContext *
qjs_new_context(JSRuntime *rt, qjs_module_t **addons)
{
    int           ret;
    JSAtom        prop;
    JSValue       global_obj;
    JSContext     *ctx;
    qjs_module_t  **module;

    ctx = JS_NewContextRaw(rt);
    if (ctx == NULL) {
        return NULL;
    }

    JS_AddIntrinsicBaseObjects(ctx);
    JS_AddIntrinsicDate(ctx);
    JS_AddIntrinsicRegExp(ctx);
    JS_AddIntrinsicJSON(ctx);
    JS_AddIntrinsicProxy(ctx);
    JS_AddIntrinsicMapSet(ctx);
    JS_AddIntrinsicTypedArrays(ctx);
    JS_AddIntrinsicPromise(ctx);
    JS_AddIntrinsicBigInt(ctx);
    JS_AddIntrinsicEval(ctx);

    for (module = qjs_modules; *module != NULL; module++) {
        if ((*module)->init(ctx, (*module)->name) == NULL) {
            return NULL;
        }
    }

    if (addons != NULL) {
        for (module = addons; *module != NULL; module++) {
            if ((*module)->init(ctx, (*module)->name) == NULL) {
                return NULL;
            }
        }
    }

    global_obj = JS_GetGlobalObject(ctx);

    JS_SetPropertyFunctionList(ctx, global_obj, qjs_global_proto,
                               njs_nitems(qjs_global_proto));

    prop = JS_NewAtom(ctx, "eval");
    if (prop == JS_ATOM_NULL) {
        return NULL;
    }

    ret = JS_DeleteProperty(ctx, global_obj, prop, 0);
    JS_FreeAtom(ctx, prop);
    if (ret < 0) {
        return NULL;
    }

    prop = JS_NewAtom(ctx, "Function");
    if (prop == JS_ATOM_NULL) {
        return NULL;
    }

    ret = JS_DeleteProperty(ctx, global_obj, prop, 0);
    JS_FreeAtom(ctx, prop);
    if (ret < 0) {
        return NULL;
    }

    JS_FreeValue(ctx, global_obj);

    return ctx;
}


static JSValue
qjs_njs_getter(JSContext *ctx, JSValueConst this_val)
{
    int      ret;
    JSValue  obj;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    ret = qjs_set_to_string_tag(ctx, obj, "njs");
    if (ret == -1) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    ret = JS_SetPropertyStr(ctx, obj, "version_number",
                            JS_NewInt32(ctx, NJS_VERSION_NUMBER));
    if (ret == -1) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    ret = JS_SetPropertyStr(ctx, obj, "version",
                            JS_NewString(ctx, NJS_VERSION));
    if (ret == -1) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    ret = JS_SetPropertyStr(ctx, obj, "engine", JS_NewString(ctx, "QuickJS"));
    if (ret == -1) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    return obj;
}


int
qjs_set_to_string_tag(JSContext *ctx, JSValueConst val, const char *tag)
{
    int      ret;
    JSAtom   atom;
    JSValue  global_obj, symbol, toStringTag;

    global_obj = JS_GetGlobalObject(ctx);

    symbol = JS_GetPropertyStr(ctx, global_obj, "Symbol");
    JS_FreeValue(ctx, global_obj);
    if (JS_IsException(symbol)) {
        return -1;
    }

    toStringTag = JS_GetPropertyStr(ctx, symbol, "toStringTag");
    if (JS_IsException(toStringTag)) {
        JS_FreeValue(ctx, symbol);
        return -1;
    }

    atom = JS_ValueToAtom(ctx, toStringTag);

    JS_FreeValue(ctx, symbol);
    JS_FreeValue(ctx, toStringTag);

    if (atom == JS_ATOM_NULL) {
        JS_ThrowInternalError(ctx, "failed to get atom");
        return -1;
    }

    ret = JS_DefinePropertyValue(ctx, val, atom, JS_NewString(ctx, tag),
                                 JS_PROP_C_W_E);

    JS_FreeAtom(ctx, atom);

    return ret;
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


JSValue
qjs_string_create_chb(JSContext *cx, njs_chb_t *chain)
{
    JSValue    val;
    njs_int_t  ret;
    njs_str_t  str;

    ret = njs_chb_join(chain, &str);
    if (ret != NJS_OK) {
        return JS_ThrowInternalError(cx, "failed to create string");
    }

    val = JS_NewStringLen(cx, (const char *) str.start, str.length);

    chain->free(cx, str.start);

    return val;
}
