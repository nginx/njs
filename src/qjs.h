
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */

#ifndef _QJS_H_INCLUDED_
#define _QJS_H_INCLUDED_

#include <njs_auto_config.h>

#include <njs_types.h>
#include <njs_clang.h>
#include <inttypes.h>
#include <string.h>
#include <njs_str.h>
#include <njs_unicode.h>
#include <njs_utf8.h>
#include <njs_chb.h>
#include <njs_utils.h>
#include <njs_assert.h>

#if defined(__GNUC__) && (__GNUC__ >= 8)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

#include <quickjs.h>

#if defined(__GNUC__) && (__GNUC__ >= 8)
#pragma GCC diagnostic pop
#endif
#include <pthread.h>


#define QJS_CORE_CLASS_ID_OFFSET    64
#define QJS_CORE_CLASS_ID_BUFFER    (QJS_CORE_CLASS_ID_OFFSET)
#define QJS_CORE_CLASS_ID_UINT8_ARRAY_CTOR (QJS_CORE_CLASS_ID_OFFSET + 1)
#define QJS_CORE_CLASS_ID_TEXT_DECODER (QJS_CORE_CLASS_ID_OFFSET + 2)
#define QJS_CORE_CLASS_ID_TEXT_ENCODER (QJS_CORE_CLASS_ID_OFFSET + 3)
#define QJS_CORE_CLASS_ID_FS_STATS  (QJS_CORE_CLASS_ID_OFFSET + 4)
#define QJS_CORE_CLASS_ID_FS_DIRENT (QJS_CORE_CLASS_ID_OFFSET + 5)
#define QJS_CORE_CLASS_ID_FS_FILEHANDLE (QJS_CORE_CLASS_ID_OFFSET + 6)
#define QJS_CORE_CLASS_ID_WEBCRYPTO_KEY (QJS_CORE_CLASS_ID_OFFSET + 7)
#define QJS_CORE_CLASS_CRYPTO_HASH (QJS_CORE_CLASS_ID_OFFSET + 8)
#define QJS_CORE_CLASS_CRYPTO_HMAC (QJS_CORE_CLASS_ID_OFFSET + 9)
#define QJS_CORE_CLASS_ID_LAST      (QJS_CORE_CLASS_ID_OFFSET + 10)


typedef JSModuleDef *(*qjs_addon_init_pt)(JSContext *ctx, const char *name);

typedef struct {
    const char                     *name;
    qjs_addon_init_pt               init;
} qjs_module_t;


JSContext *qjs_new_context(JSRuntime *rt, qjs_module_t **addons);


JSValue qjs_new_uint8_array(JSContext *ctx, int argc, JSValueConst *argv);
JSValue qjs_new_array_buffer(JSContext *cx, uint8_t *src, size_t len);
JSValue qjs_buffer_alloc(JSContext *ctx, size_t size);
JSValue qjs_buffer_create(JSContext *ctx, u_char *start, size_t size);
JSValue qjs_buffer_chb_alloc(JSContext *ctx, njs_chb_t *chain);

typedef int (*qjs_buffer_encode_t)(JSContext *ctx, const njs_str_t *src,
    njs_str_t *dst);
typedef size_t (*qjs_buffer_encode_length_t)(JSContext *ctx,
    const njs_str_t *src);

typedef struct {
    njs_str_t                   name;
    qjs_buffer_encode_t         encode;
    qjs_buffer_encode_length_t  encode_length;
    qjs_buffer_encode_t         decode;
    qjs_buffer_encode_length_t  decode_length;
} qjs_buffer_encoding_t;

const qjs_buffer_encoding_t *qjs_buffer_encoding(JSContext *ctx,
    JSValueConst value, JS_BOOL thrw);

int qjs_base64_encode(JSContext *ctx, const njs_str_t *src,
    njs_str_t *dst);
size_t qjs_base64_encode_length(JSContext *ctx, const njs_str_t *src);
int qjs_base64_decode(JSContext *ctx, const njs_str_t *src,
    njs_str_t *dst);
size_t qjs_base64_decode_length(JSContext *ctx, const njs_str_t *src);
int qjs_base64url_encode(JSContext *ctx, const njs_str_t *src,
    njs_str_t *dst);
int qjs_base64url_decode(JSContext *ctx, const njs_str_t *src,
    njs_str_t *dst);
size_t qjs_base64url_decode_length(JSContext *ctx, const njs_str_t *src);
int qjs_hex_encode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst);
size_t qjs_hex_encode_length(JSContext *ctx, const njs_str_t *src);
int qjs_hex_decode(JSContext *ctx, const njs_str_t *src, njs_str_t *dst);
size_t qjs_hex_decode_length(JSContext *ctx, const njs_str_t *src);

JSValue qjs_process_object(JSContext *ctx, int argc, const char **argv);

typedef struct {
    int                         tag;
    size_t                      length;
    u_char                      *start;
} qjs_bytes_t;

int qjs_to_bytes(JSContext *ctx, qjs_bytes_t *data, JSValueConst value);
void qjs_bytes_free(JSContext *ctx, qjs_bytes_t *data);
JSValue qjs_typed_array_data(JSContext *ctx, JSValueConst value,
    njs_str_t *data);

#define qjs_string_create(ctx, data, len)                                   \
    JS_NewStringLen(ctx, (const char *) (data), len)
JSValue qjs_string_create_chb(JSContext *cx, njs_chb_t *chain);

/* qjs_base64_encoded_length(512) */
#define QJS_ENCODE_BUF_LEN 686
JSValue qjs_string_hex(JSContext *cx, const njs_str_t *src);
JSValue qjs_string_base64(JSContext *cx, const njs_str_t *src);
JSValue qjs_string_base64url(JSContext *cx, const njs_str_t *src);

static inline JS_BOOL JS_IsNullOrUndefined(JSValueConst v)
{
    return JS_VALUE_GET_TAG(v) == JS_TAG_NULL
           || JS_VALUE_GET_TAG(v) == JS_TAG_UNDEFINED;
}


#ifdef NJS_HAVE_QUICKJS_IS_SAME_VALUE
#define qjs_is_same_value(cx, a, b) JS_IsSameValue(cx, a, b)
#else
#define qjs_is_same_value(cx, a, b) JS_SameValue(cx, a, b)
#endif


extern qjs_module_t              *qjs_modules[];

#endif /* _QJS_H_INCLUDED_ */
