
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

#if defined(__GNUC__) && (__GNUC__ >= 8)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

#include <quickjs.h>

#if defined(__GNUC__) && (__GNUC__ >= 8)
#pragma GCC diagnostic pop
#endif
#define NJS_QUICKJS_VERSION  "Unknown version"
#include <pthread.h>


#define QJS_CORE_CLASS_ID_OFFSET  64
#define QJS_CORE_CLASS_ID_BUFFER  (QJS_CORE_CLASS_ID_OFFSET)
#define QJS_CORE_CLASS_ID_UINT8_ARRAY_CTOR (QJS_CORE_CLASS_ID_OFFSET + 1)
#define QJS_CORE_CLASS_ID_LAST    (QJS_CORE_CLASS_ID_UINT8_ARRAY_CTOR)


typedef JSModuleDef *(*qjs_addon_init_pt)(JSContext *ctx, const char *name);

typedef struct {
    const char                     *name;
    qjs_addon_init_pt               init;
} qjs_module_t;


JSContext *qjs_new_context(JSRuntime *rt, qjs_module_t **addons);


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


static inline JS_BOOL JS_IsNullOrUndefined(JSValueConst v)
{
    return JS_VALUE_GET_TAG(v) == JS_TAG_NULL
           || JS_VALUE_GET_TAG(v) == JS_TAG_UNDEFINED;
}


extern qjs_module_t              *qjs_modules[];

#endif /* _QJS_H_INCLUDED_ */
