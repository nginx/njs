
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */


#include <qjs.h>

static JSValue qjs_query_string_parse(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_query_string_stringify(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_query_string_escape(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_query_string_unescape(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_query_string_decode(JSContext *cx, const u_char *start,
    size_t size);
static int qjs_query_string_encode(njs_chb_t *chain, njs_str_t *str);
static JSValue qjs_query_string_parser(JSContext *cx, u_char *query,
    u_char *end, njs_str_t *sep, njs_str_t *eq, JSValue decode,
    unsigned max_keys);
static JSModuleDef *qjs_querystring_init(JSContext *ctx, const char *name);


static const JSCFunctionListEntry  qjs_querystring_export[] = {
    JS_CFUNC_DEF("decode", 4, qjs_query_string_parse),
    JS_CFUNC_DEF("encode", 4, qjs_query_string_stringify),
    JS_CFUNC_DEF("escape", 1, qjs_query_string_escape),
    JS_CFUNC_DEF("parse", 4, qjs_query_string_parse),
    JS_CFUNC_DEF("stringify", 4, qjs_query_string_stringify),
    JS_CFUNC_DEF("unescape", 1, qjs_query_string_unescape),
};


qjs_module_t  qjs_query_string_module = {
    .name = "querystring",
    .init = qjs_querystring_init,
};


static JSValue
qjs_query_string_parse(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int64_t    max_keys;
    JSValue    options, ret, decode, native;
    njs_str_t  str, sep, eq;

    sep.start = NULL;
    eq.start = NULL;
    str.start = NULL;

    max_keys = 1000;
    decode = JS_UNDEFINED;

    if (!JS_IsNullOrUndefined(argv[1])) {
        sep.start = (u_char *) JS_ToCStringLen(cx, &sep.length, argv[1]);
        if (sep.start == NULL) {
            return JS_EXCEPTION;
        }
    }

    if (!JS_IsNullOrUndefined(argv[2])) {
        eq.start = (u_char *) JS_ToCStringLen(cx, &eq.length, argv[2]);
        if (eq.start == NULL) {
            JS_FreeCString(cx, (char *) sep.start);
            return JS_EXCEPTION;
        }
    }

    options = argv[3];
    if (JS_IsObject(options)) {
        ret = JS_GetPropertyStr(cx, options, "maxKeys");
        if (JS_IsException(ret)) {
            goto fail;
        }

        if (!JS_IsUndefined(ret)) {
            if (JS_ToInt64(cx, &max_keys, ret) < 0) {
                JS_FreeValue(cx, ret);
                goto fail;
            }

            JS_FreeValue(cx, ret);

            if (max_keys < 0) {
                max_keys = INT64_MAX;
            }
        }

        decode = JS_GetPropertyStr(cx, options, "decodeURIComponent");
        if (JS_IsException(decode)) {
            goto fail;
        }

        if (!JS_IsUndefined(decode) && !JS_IsFunction(cx, decode)) {
            JS_ThrowTypeError(cx, "option decodeURIComponent is not "
                              "a function");
            goto fail;
        }
    }

    if (JS_IsNullOrUndefined(decode)) {
        decode = JS_GetPropertyStr(cx, this_val, "unescape");
        if (JS_IsException(decode)) {
            goto fail;
        }

        if (!JS_IsFunction(cx, decode)) {
            JS_ThrowTypeError(cx, "QueryString.unescape is not a function");
            goto fail;
        }

        native = JS_GetPropertyStr(cx, decode, "native");
        if (JS_IsException(native)) {
            goto fail;
        }

        if (JS_IsBool(native)) {
            JS_FreeValue(cx, decode);
            decode = JS_NULL;
        }
    }

    str.start = (u_char *) JS_ToCStringLen(cx, &str.length, argv[0]);
    if (str.start == NULL) {
        goto fail;
    }

    ret = qjs_query_string_parser(cx, str.start, str.start + str.length,
                                  sep.start ? &sep : NULL,
                                  eq.start ? &eq : NULL, decode, max_keys);

    JS_FreeValue(cx, decode);

    if (sep.start != NULL) {
        JS_FreeCString(cx, (char *) sep.start);
    }

    if (eq.start != NULL) {
        JS_FreeCString(cx, (char *) eq.start);
    }

    JS_FreeCString(cx, (char *) str.start);

    return ret;

fail:

    JS_FreeValue(cx, decode);

    if (sep.start != NULL) {
        JS_FreeCString(cx, (char *) sep.start);
    }

    if (eq.start != NULL) {
        JS_FreeCString(cx, (char *) eq.start);
    }

    if (str.start != NULL) {
        JS_FreeCString(cx, (char *) str.start);
    }

    return JS_EXCEPTION;
}


static JSValue
qjs_query_string_decode(JSContext *cx, const u_char *start, size_t size)
{
    u_char                *dst;
    uint32_t              cp;
    njs_chb_t             chain;
    const u_char          *p, *end;
    njs_unicode_decode_t  ctx;

    static const int8_t  hex[256]
        njs_aligned(32) =
    {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    NJS_CHB_CTX_INIT(&chain, cx);
    njs_utf8_decode_init(&ctx);

    cp = 0;

    p = start;
    end = p + size;

    while (p < end) {
        if (*p == '%' && end - p > 2 && hex[p[1]] >= 0 && hex[p[2]] >= 0) {
            cp = njs_utf8_consume(&ctx, (hex[p[1]] << 4) | hex[p[2]]);
            p += 3;

        } else {
            if (*p == '+') {
                cp = ' ';
                p++;

            } else {
                cp = njs_utf8_decode(&ctx, &p, end);
            }
        }

        if (cp > NJS_UNICODE_MAX_CODEPOINT) {
            if (cp == NJS_UNICODE_CONTINUE) {
                continue;
            }

            cp = NJS_UNICODE_REPLACEMENT;
        }

        dst = njs_chb_reserve(&chain, 4);
        if (dst == NULL) {
            JS_ThrowOutOfMemory(cx);
            return JS_EXCEPTION;
        }

        njs_chb_written(&chain, njs_utf8_encode(dst, cp) - dst);
    }

    if (cp == NJS_UNICODE_CONTINUE) {
        dst = njs_chb_reserve(&chain, 3);
        if (dst == NULL) {
            JS_ThrowOutOfMemory(cx);
            return JS_EXCEPTION;
        }

        njs_chb_written(&chain,
                        njs_utf8_encode(dst, NJS_UNICODE_REPLACEMENT) - dst);
    }


    return qjs_string_create_chb(cx, &chain);
}


static JSValue
qjs_query_string_escape(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue    ret;
    njs_str_t  str;
    njs_chb_t  chain;

    str.start = (u_char *) JS_ToCStringLen(cx, &str.length, argv[0]);
    if (str.start == NULL) {
        return JS_EXCEPTION;
    }

    NJS_CHB_CTX_INIT(&chain, cx);

    if (qjs_query_string_encode(&chain, &str) < 0) {
        JS_FreeCString(cx, (char *) str.start);
        njs_chb_destroy(&chain);
        return JS_EXCEPTION;
    }

    ret = qjs_string_create_chb(cx, &chain);

    JS_FreeCString(cx, (char *) str.start);

    return ret;
}


static JSValue
qjs_query_string_unescape(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    u_char     *p, *end;
    JSValue    ret;
    njs_str_t  str;

    str.start = (u_char *) JS_ToCStringLen(cx, &str.length, argv[0]);
    if (str.start == NULL) {
        return JS_EXCEPTION;
    }

    p = str.start;
    end = p + str.length;

    ret = qjs_query_string_decode(cx, p, end - p);

    JS_FreeCString(cx, (char *) str.start);

    return ret;
}


static u_char *
qjs_query_string_match(u_char *p, u_char *end, const njs_str_t *v)
{
    size_t  length;

    length = v->length;

    if (length == 1) {
        p = njs_strlchr(p, end, v->start[0]);

        if (p == NULL) {
            p = end;
        }

        return p;
    }

    while (p <= (end - length)) {
        if (memcmp(p, v->start, length) == 0) {
            return p;
        }

        p++;
    }

    return end;
}


static int
qjs_query_string_append(JSContext *cx, JSValue object, const u_char *key,
    size_t key_size, const u_char *val, size_t val_size, JSValue decoder)
{
    JSAtom    prop;
    JSValue   name, value, prev, length, ret;
    uint32_t  len;

    if (JS_IsNullOrUndefined(decoder)) {
        name = qjs_query_string_decode(cx, key, key_size);
        if (JS_IsException(name)) {
            return -1;
        }

        value = qjs_query_string_decode(cx, val, val_size);
        if (JS_IsException(value)) {
            JS_FreeValue(cx, name);
            return -1;
        }

    } else {
        name = JS_NewStringLen(cx, (const char *) key, key_size);
        if (JS_IsException(name)) {
            return -1;
        }

        ret = JS_Call(cx, decoder, JS_UNDEFINED, 1, &name);
        JS_FreeValue(cx, name);
        if (JS_IsException(ret)) {
            return -1;
        }

        name = ret;

        value = JS_NewStringLen(cx, (const char *) val, val_size);
        if (JS_IsException(value)) {
            return -1;
        }

        ret = JS_Call(cx, decoder, JS_UNDEFINED, 1, &value);
        JS_FreeValue(cx, value);
        if (JS_IsException(ret)) {
            JS_FreeValue(cx, name);
            return -1;
        }

        value = ret;
    }

    prop = JS_ValueToAtom(cx, name);
    JS_FreeValue(cx, name);
    if (prop == JS_ATOM_NULL) {
        JS_FreeValue(cx, value);
        return -1;
    }

    prev = JS_GetProperty(cx, object, prop);
    if (JS_IsException(prev)) {
        JS_FreeAtom(cx, prop);
        JS_FreeValue(cx, value);
        return -1;
    }

    if (JS_IsUndefined(prev)) {
        if (JS_SetProperty(cx, object, prop, value) < 0) {
            goto exception;
        }

    } else if (qjs_is_array(cx, prev)) {
        length = JS_GetPropertyStr(cx, prev, "length");

        if (JS_ToUint32(cx, &len, length) < 0) {
            JS_FreeValue(cx, length);
            goto exception;
        }

        JS_FreeValue(cx, length);

        if (JS_SetPropertyUint32(cx, prev, len, value) < 0) {
            goto exception;
        }

        JS_FreeValue(cx, prev);

    } else {
        ret = JS_NewArray(cx);
        if (JS_IsException(ret)) {
            goto exception;
        }

        if (JS_SetPropertyUint32(cx, ret, 0, prev) < 0) {
            JS_FreeValue(cx, ret);
            goto exception;
        }

        prev = JS_UNDEFINED;

        if (JS_SetPropertyUint32(cx, ret, 1, value) < 0) {
            JS_FreeValue(cx, ret);
            goto exception;
        }

        value = JS_UNDEFINED;

        if (JS_SetProperty(cx, object, prop, ret) < 0) {
            JS_FreeValue(cx, ret);
            goto exception;
        }
    }

    JS_FreeAtom(cx, prop);

    return 0;

exception:

    JS_FreeAtom(cx, prop);
    JS_FreeValue(cx, prev);
    JS_FreeValue(cx, value);

    return -1;
}


static JSValue
qjs_query_string_parser(JSContext *cx, u_char *query, u_char *end,
    njs_str_t *sep, njs_str_t *eq, JSValue decode, unsigned max_keys)
{
    size_t     size;
    u_char     *part, *key, *val;
    JSValue    obj;
    unsigned   count;
    njs_str_t  sep_val, eq_val;

    if (sep == NULL || sep->length == 0) {
        sep = &sep_val;
        sep->start = (u_char *) "&";
        sep->length = 1;
    }

    if (eq == NULL || eq->length == 0) {
        eq = &eq_val;
        eq->start = (u_char *) "=";
        eq->length = 1;
    }

    obj = JS_NewObject(cx);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    count = 0;

    key = query;

    while (key < end) {
        if (count++ == max_keys) {
            break;
        }

        part = qjs_query_string_match(key, end, sep);

        if (part == key) {
            goto next;
        }

        val = qjs_query_string_match(key, part, eq);

        size = val - key;

        if (val != part) {
            val += eq->length;
        }

        if (qjs_query_string_append(cx, obj, key, size, val, part - val,
                                     decode) < 0)
        {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

next:

        key = part + sep->length;
    }

    return obj;
}


static inline int
qjs_need_escape(const uint32_t *escape, uint32_t byte)
{
    return ((escape[byte >> 5] & ((uint32_t) 1 << (byte & 0x1f))) != 0);
}


static inline u_char *
qjs_string_encode(const uint32_t *escape, size_t size, const u_char *src,
    u_char *dst)
{
    uint8_t              byte;
    static const u_char  hex[16] = "0123456789ABCDEF";

    do {
        byte = *src++;

        if (qjs_need_escape(escape, byte)) {
            *dst++ = '%';
            *dst++ = hex[byte >> 4];
            *dst++ = hex[byte & 0xf];

        } else {
            *dst++ = byte;
        }

        size--;

    } while (size != 0);

    return dst;
}


static int
qjs_query_string_encode(njs_chb_t *chain, njs_str_t *str)
{
    size_t  size;
    u_char  *p, *start, *end;

    static const uint32_t  escape[] = {
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */

                     /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
        0xfc00987d,  /* 1111 1100 0000 0000  1001 1000 0111 1101 */

                     /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
        0x78000001,  /* 0111 1000 0000 0000  0000 0000 0000 0001 */

                     /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
        0xb8000001,  /* 1011 1000 0000 0000  0000 0000 0000 0001 */

        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff,  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
    };

    if (chain->error) {
        return -1;
    }

    if (str->length == 0) {
        return 0;
    }

    p = str->start;
    end = p + str->length;

    size = str->length;
    while (p < end) {
        if (qjs_need_escape(escape, *p++)) {
            size += 2;
        }
    }

    start = njs_chb_reserve(chain, size);
    if (start == NULL) {
        return -1;
    }

    if (size == str->length) {
        memcpy(start, str->start, str->length);
        njs_chb_written(chain, str->length);
        return 0;
    }

    qjs_string_encode(escape, str->length, str->start, start);

    njs_chb_written(chain, size);

    return 0;
}


static inline int
qjs_query_string_encoder_call(JSContext *cx, njs_chb_t *chain,
    JSValue encoder, JSValue value)
{
    int        rc;
    JSValue    ret;
    njs_str_t  str;

    if (JS_IsNullOrUndefined(encoder)) {
        str.start = (u_char *) JS_ToCStringLen(cx, &str.length, value);
        if (str.start == NULL) {
            return -1;
        }

        rc = qjs_query_string_encode(chain, &str);
        JS_FreeCString(cx, (char *) str.start);
        return rc;
    }

    ret = JS_Call(cx, encoder, JS_UNDEFINED, 1, &value);
    if (JS_IsException(ret)) {
        return -1;
    }

    str.start = (u_char *) JS_ToCStringLen(cx, &str.length, ret);
    JS_FreeValue(cx, ret);
    if (str.start == NULL) {
        return -1;
    }

    njs_chb_append_str(chain, &str);

    JS_FreeCString(cx, (char *) str.start);

    return 0;
}


static inline int
qjs_query_string_push(JSContext *cx, njs_chb_t *chain, JSValue key,
    JSValue value, njs_str_t *eq, JSValue encoder)
{
    if (qjs_query_string_encoder_call(cx, chain, encoder, key) < 0) {
        return -1;
    }

    njs_chb_append(chain, eq->start, eq->length);

    if (JS_IsNumber(value)
        || JS_IsBool(value)
        || JS_IsString(value))
    {
        return qjs_query_string_encoder_call(cx, chain, encoder, value);
    }

    return 0;
}


static inline int
qjs_query_string_push_array(JSContext *cx, njs_chb_t *chain, JSValue key,
    JSValue array, njs_str_t *eq, njs_str_t *sep, JSValue encoder)
{
    int       rc;
    JSValue   val, len;
    uint32_t  i, length;

    len = JS_GetPropertyStr(cx, array, "length");
    if (JS_IsException(len)) {
        return -1;
    }

    if (JS_ToUint32(cx, &length, len) < 0) {
        JS_FreeValue(cx, len);
        return -1;
    }

    JS_FreeValue(cx, len);

    for (i = 0; i < length; i++) {
        if (chain->last != NULL) {
            njs_chb_append(chain, sep->start, sep->length);
        }

        val = JS_GetPropertyUint32(cx, array, i);
        if (JS_IsException(val)) {
            return -1;
        }

        rc = qjs_query_string_push(cx, chain, key, val, eq, encoder);
        JS_FreeValue(cx, val);
        if (rc != 0) {
            return -1;
        }
    }

    return 0;
}


static JSValue
qjs_query_string_stringify_internal(JSContext *cx, JSValue obj, njs_str_t *sep,
    njs_str_t *eq, JSValue encoder)
{
    int             rc;
    uint32_t        n, length;
    JSValue         key, val;
    njs_str_t       sep_val, eq_val;
    njs_chb_t       chain;
    JSPropertyEnum  *ptab;

    if (!JS_IsObject(obj)) {
        return JS_NewString(cx, "");
    }

    if (JS_GetOwnPropertyNames(cx, &ptab, &length, obj,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)
        < 0)
    {
        return JS_EXCEPTION;
    }

    if (sep == NULL || sep->length == 0) {
        sep = &sep_val;
        sep->start = (u_char *) "&";
        sep->length = 1;
    }

    if (eq == NULL || eq->length == 0) {
        eq = &eq_val;
        eq->start = (u_char *) "=";
        eq->length = 1;
    }

    NJS_CHB_CTX_INIT(&chain, cx);

    for (n = 0; n < length; n++) {
        val = JS_GetProperty(cx, obj, ptab[n].atom);
        if (JS_IsException(val)) {
            goto fail;
        }

        if (qjs_is_array(cx, val)) {
            key = JS_AtomToString(cx, ptab[n].atom);
            if (JS_IsException(key)) {
                JS_FreeValue(cx, val);
                goto fail;
            }

            rc = qjs_query_string_push_array(cx, &chain, key, val, eq, sep,
                                             encoder);
            JS_FreeValue(cx, key);
            JS_FreeValue(cx, val);
            if (rc != 0) {
                goto fail;
            }

            continue;
        }

        if (n != 0) {
            njs_chb_append(&chain, sep->start, sep->length);
        }

        key = JS_AtomToString(cx, ptab[n].atom);
        if (JS_IsException(key)) {
            JS_FreeValue(cx, val);
            goto fail;
        }

        rc = qjs_query_string_push(cx, &chain, key, val, eq, encoder);
        JS_FreeValue(cx, key);
        JS_FreeValue(cx, val);
        if (rc != 0) {
            goto fail;
        }
    }

    if (ptab != NULL) {
        qjs_free_prop_enum(cx, ptab, length);
    }

    return qjs_string_create_chb(cx, &chain);

fail:

    if (ptab != NULL) {
        qjs_free_prop_enum(cx, ptab, length);
    }

    njs_chb_destroy(&chain);

    return JS_EXCEPTION;
}


static JSValue
qjs_query_string_stringify(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue    options, ret, encode, native;
    njs_str_t  sep, eq;

    sep.start = NULL;
    eq.start = NULL;

    encode = JS_UNDEFINED;

    if (!JS_IsNullOrUndefined(argv[1])) {
        sep.start = (u_char *) JS_ToCStringLen(cx, &sep.length, argv[1]);
        if (sep.start == NULL) {
            return JS_EXCEPTION;
        }
    }

    if (!JS_IsNullOrUndefined(argv[2])) {
        eq.start = (u_char *) JS_ToCStringLen(cx, &eq.length, argv[2]);
        if (eq.start == NULL) {
            JS_FreeCString(cx, (char *) sep.start);
            return JS_EXCEPTION;
        }
    }

    options = argv[3];
    if (JS_IsObject(options)) {
        encode = JS_GetPropertyStr(cx, options, "encodeURIComponent");
        if (JS_IsException(encode)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(encode) && !JS_IsFunction(cx, encode)) {
            JS_ThrowTypeError(cx, "option encodeURIComponent is not "
                              "a function");
            goto fail;
        }
    }

    if (JS_IsNullOrUndefined(encode)) {
        encode = JS_GetPropertyStr(cx, this_val, "escape");
        if (JS_IsException(encode)) {
            goto fail;
        }

        if (!JS_IsFunction(cx, encode)) {
            JS_ThrowTypeError(cx, "QueryString.escape is not a function");
            goto fail;
        }

        native = JS_GetPropertyStr(cx, encode, "native");
        if (JS_IsException(native)) {
            goto fail;
        }

        if (JS_IsBool(native)) {
            JS_FreeValue(cx, encode);
            encode = JS_NULL;
        }
    }

    ret = qjs_query_string_stringify_internal(cx, argv[0],
                                              sep.start ? &sep : NULL,
                                              eq.start ? &eq : NULL, encode);

    JS_FreeValue(cx, encode);

    if (sep.start != NULL) {
        JS_FreeCString(cx, (char *) sep.start);
    }

    if (eq.start != NULL) {
        JS_FreeCString(cx, (char *) eq.start);
    }

    return ret;

fail:

    JS_FreeValue(cx, encode);

    if (sep.start != NULL) {
        JS_FreeCString(cx, (char *) sep.start);
    }

    if (eq.start != NULL) {
        JS_FreeCString(cx, (char *) eq.start);
    }

    return JS_EXCEPTION;
}


static int
qjs_querystring_module_init(JSContext *ctx, JSModuleDef *m)
{
    int      rc;
    JSValue  proto, method;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return -1;
    }

    JS_SetPropertyFunctionList(ctx, proto, qjs_querystring_export,
                               njs_nitems(qjs_querystring_export));

    method = JS_GetPropertyStr(ctx, proto, "escape");
    if (JS_IsException(method)) {
        return -1;
    }

    /* Marking the default "escape" function for the fast path. */

    if (JS_SetPropertyStr(ctx, method, "native", JS_NewBool(ctx, 1)) < 0) {
        JS_FreeValue(ctx, method);
        return -1;
    }

    JS_FreeValue(ctx, method);

    method = JS_GetPropertyStr(ctx, proto, "unescape");
    if (JS_IsException(method)) {
        return -1;
    }

    /* Marking the default "unescape" function for the fast path. */

    if (JS_SetPropertyStr(ctx, method, "native", JS_NewBool(ctx, 1)) < 0) {
        JS_FreeValue(ctx, method);
        return -1;
    }

    JS_FreeValue(ctx, method);

    rc = JS_SetModuleExport(ctx, m, "default", proto);
    if (rc != 0) {
        return -1;
    }

    return JS_SetModuleExportList(ctx, m, qjs_querystring_export,
                                  njs_nitems(qjs_querystring_export));
}


static JSModuleDef *
qjs_querystring_init(JSContext *ctx, const char *name)
{
    int          rc;
    JSModuleDef  *m;

    m = JS_NewCModule(ctx, name, qjs_querystring_module_init);
    if (m == NULL) {
        return NULL;
    }

    if (JS_AddModuleExport(ctx, m, "default") < 0) {
        return NULL;
    }

    rc = JS_AddModuleExportList(ctx, m, qjs_querystring_export,
                                njs_nitems(qjs_querystring_export));
    if (rc != 0) {
        return NULL;
    }

    return m;
}
