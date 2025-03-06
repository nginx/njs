
/*
 * Copyright (C) Vadim Zhestkov
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>
#include "njs_hash.h"

typedef void (*qjs_hash_init)(njs_hash_t *ctx);
typedef void (*qjs_hash_update)(njs_hash_t *ctx, const void *data, size_t size);
typedef void (*qjs_hash_final)(u_char result[32], njs_hash_t *ctx);

typedef JSValue (*qjs_digest_encode)(JSContext *cx, const njs_str_t *src);


typedef struct {
    njs_str_t      name;

    size_t         size;
    qjs_hash_init  init;
    qjs_hash_update update;
    qjs_hash_final final;
} qjs_hash_alg_t;

typedef struct {
    njs_hash_t     ctx;
    qjs_hash_alg_t *alg;
} qjs_digest_t;

typedef struct {
    u_char         opad[64];
    njs_hash_t     ctx;
    qjs_hash_alg_t *alg;
} qjs_hmac_t;


typedef struct {
    njs_str_t          name;

    qjs_digest_encode  encode;
} qjs_crypto_enc_t;


static qjs_hash_alg_t *qjs_crypto_algorithm(JSContext *cx, JSValueConst val);
static qjs_crypto_enc_t *qjs_crypto_encoding(JSContext *cx, JSValueConst val);
static JSValue qjs_buffer_digest(JSContext *cx, const njs_str_t *src);
static JSValue qjs_crypto_create_hash(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_hash_prototype_update(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int hmac);
static JSValue qjs_hash_prototype_digest(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int hmac);
static JSValue qjs_hash_prototype_copy(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_crypto_create_hmac(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static void qjs_hash_finalizer(JSRuntime *rt, JSValue val);
static void qjs_hmac_finalizer(JSRuntime *rt, JSValue val);
static int qjs_crypto_module_init(JSContext *cx, JSModuleDef *m);
static JSModuleDef * qjs_crypto_init(JSContext *cx, const char *module_name);


static qjs_hash_alg_t qjs_hash_algorithms[] = {

    {
        njs_str("md5"),
        16,
        njs_md5_init,
        njs_md5_update,
        njs_md5_final
    },

    {
        njs_str("sha1"),
        20,
        njs_sha1_init,
        njs_sha1_update,
        njs_sha1_final
    },

    {
        njs_str("sha256"),
        32,
        njs_sha2_init,
        njs_sha2_update,
        njs_sha2_final
    },

    {
        njs_null_str,
        0,
        NULL,
        NULL,
        NULL
    }

};


static qjs_crypto_enc_t qjs_encodings[] = {

    {
        njs_str("buffer"),
        qjs_buffer_digest
    },

    {
        njs_str("hex"),
        qjs_string_hex
    },

    {
        njs_str("base64"),
        qjs_string_base64
    },

    {
        njs_str("base64url"),
        qjs_string_base64url
    },

    {
        njs_null_str,
        NULL
    }

};


static const JSCFunctionListEntry qjs_crypto_export[] = {
    JS_CFUNC_DEF("createHash", 1, qjs_crypto_create_hash),
    JS_CFUNC_DEF("createHmac", 2, qjs_crypto_create_hmac),
};


static const JSCFunctionListEntry qjs_hash_proto_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Hash", JS_PROP_CONFIGURABLE),
    JS_CFUNC_MAGIC_DEF("update", 2, qjs_hash_prototype_update, 0),
    JS_CFUNC_MAGIC_DEF("digest", 1, qjs_hash_prototype_digest, 0),
    JS_CFUNC_DEF("copy", 0, qjs_hash_prototype_copy),
    JS_CFUNC_DEF("constructor", 1, qjs_crypto_create_hash),
};


static const JSCFunctionListEntry qjs_hmac_proto_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Hmac", JS_PROP_CONFIGURABLE),
    JS_CFUNC_MAGIC_DEF("update", 2, qjs_hash_prototype_update, 1),
    JS_CFUNC_MAGIC_DEF("digest", 1, qjs_hash_prototype_digest, 1),
    JS_CFUNC_DEF("constructor", 2, qjs_crypto_create_hmac),
};


static JSClassDef qjs_hash_class = {
    "Hash",
    .finalizer = qjs_hash_finalizer,
};


static JSClassDef qjs_hmac_class = {
    "Hmac",
    .finalizer = qjs_hmac_finalizer,
};


qjs_module_t qjs_crypto_module = {
    .name = "crypto",
    .init = qjs_crypto_init,
};


static JSValue
qjs_crypto_create_hash(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue         obj;
    qjs_digest_t    *dgst;
    qjs_hash_alg_t  *alg;

    alg = qjs_crypto_algorithm(cx, argv[0]);
    if (alg == NULL) {
        return JS_EXCEPTION;
    }

    dgst = js_malloc(cx, sizeof(qjs_digest_t));
    if (dgst == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    dgst->alg = alg;
    alg->init(&dgst->ctx);

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_CRYPTO_HASH);
    if (JS_IsException(obj)) {
        js_free(cx, dgst);
        return obj;
    }

    JS_SetOpaque(obj, dgst);

    return obj;
}


static JSValue
qjs_hash_prototype_update(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int hmac)
{
    njs_str_t                    str, content;
    njs_hash_t                   *uctx;
    qjs_hmac_t                   *hctx;
    qjs_bytes_t                  bytes;
    qjs_digest_t                 *dgst;
    const qjs_buffer_encoding_t  *enc;

    void (*update)(njs_hash_t *ctx, const void *data, size_t size);

    if (!hmac) {
        dgst = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_CRYPTO_HASH);
        if (dgst == NULL) {
            return JS_ThrowTypeError(cx, "\"this\" is not a hash object");
        }

        if (dgst->alg == NULL) {
            return JS_ThrowTypeError(cx, "Digest already called");
        }

        update = dgst->alg->update;
        uctx = &dgst->ctx;

    } else {
        hctx = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_CRYPTO_HMAC);
        if (hctx == NULL) {
            return JS_ThrowTypeError(cx, "\"this\" is not a hmac object");
        }

        if (hctx->alg == NULL) {
            return JS_ThrowTypeError(cx, "Digest already called");
        }

        update = hctx->alg->update;
        uctx = &hctx->ctx;
    }

    if (JS_IsString(argv[0])) {
        enc = qjs_buffer_encoding(cx, argv[1], 1);
        if (enc == NULL) {
            return JS_EXCEPTION;
        }

        str.start = (u_char *) JS_ToCStringLen(cx, &str.length, argv[0]);
        if (str.start == NULL) {
            return JS_EXCEPTION;
        }

        if (enc->decode_length != NULL) {
            content.length = enc->decode_length(cx, &str);
            content.start = js_malloc(cx, content.length);
            if (content.start == NULL) {
                JS_FreeCString(cx, (const char *) str.start);
                return JS_ThrowOutOfMemory(cx);
            }

            if (enc->decode(cx, &str, &content) != 0) {
                JS_FreeCString(cx, (const char *) str.start);
                JS_FreeCString(cx, (const char *) content.start);
                return JS_EXCEPTION;
            }

            JS_FreeCString(cx, (const char *) str.start);

            update(uctx, content.start, content.length);
            js_free(cx, content.start);

        } else {
           update(uctx, str.start, str.length);
           JS_FreeCString(cx, (const char *) str.start);
        }

    } else if (qjs_is_typed_array(cx, argv[0])) {
        if (qjs_to_bytes(cx, &bytes, argv[0]) != 0) {
            return JS_EXCEPTION;
        }

        update(uctx, bytes.start, bytes.length);

    } else {
        return JS_ThrowTypeError(cx,
                                 "data is not a string or Buffer-like object");
    }

    return JS_DupValue(cx, this_val);
}


static JSValue
qjs_hash_prototype_digest(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int hmac)
{
    njs_str_t         str;
    qjs_hmac_t        *hctx;
    qjs_digest_t      *dgst;
    qjs_hash_alg_t    *alg;
    qjs_crypto_enc_t  *enc;
    u_char            hash1[32],digest[32];

    if (!hmac) {
        dgst = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_CRYPTO_HASH);
        if (dgst == NULL) {
            return JS_ThrowTypeError(cx, "\"this\" is not a hash object");
        }

        alg = dgst->alg;
        if (alg == NULL) {
            return JS_ThrowTypeError(cx, "Digest already called");
        }

        dgst->alg = NULL;

        alg->final(digest, &dgst->ctx);

    } else {
        hctx = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_CRYPTO_HMAC);
        if (hctx == NULL) {
            return JS_ThrowTypeError(cx, "\"this\" is not a hmac object");
        }

        alg = hctx->alg;
        if (alg == NULL) {
            return JS_ThrowTypeError(cx, "Digest already called");
        }

        hctx->alg = NULL;

        alg->final(hash1, &hctx->ctx);

        alg->init(&hctx->ctx);
        alg->update(&hctx->ctx, hctx->opad, 64);
        alg->update(&hctx->ctx, hash1, alg->size);
        alg->final(digest, &hctx->ctx);
    }

    str.start = digest;
    str.length = alg->size;

    if (argc == 0) {
        return qjs_buffer_digest(cx, &str);
    }

    enc = qjs_crypto_encoding(cx, argv[0]);
    if (enc == NULL) {
        return JS_EXCEPTION;
    }

    return enc->encode(cx, &str);
}


static JSValue
qjs_hash_prototype_copy(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue       obj;
    qjs_digest_t  *dgst, *copy;

    dgst = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_CRYPTO_HASH);
    if (dgst == NULL) {
        return JS_EXCEPTION;
    }

    if (dgst->alg == NULL) {
        return JS_ThrowTypeError(cx, "Digest already called");
    }

    copy = js_malloc(cx, sizeof(qjs_digest_t));
    if (copy == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    memcpy(copy, dgst, sizeof(qjs_digest_t));

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_CRYPTO_HASH);
    if (JS_IsException(obj)) {
        js_free(cx, copy);
        return obj;
    }

    JS_SetOpaque(obj, copy);

    return obj;
}


static JSValue
qjs_crypto_create_hmac(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int             i;
    JS_BOOL         key_is_string;
    JSValue         obj;
    njs_str_t       key;
    qjs_hmac_t      *hmac;
    qjs_bytes_t     bytes;
    qjs_hash_alg_t  *alg;
    u_char          digest[32], key_buf[64];

    alg = qjs_crypto_algorithm(cx, argv[0]);
    if (alg == NULL) {
        return JS_EXCEPTION;
    }

    key_is_string = JS_IsString(argv[1]);
    if (key_is_string) {
        key.start = (u_char *) JS_ToCStringLen(cx, &key.length, argv[1]);
        if (key.start == NULL) {
            return JS_EXCEPTION;
        }

    } else if (qjs_is_typed_array(cx, argv[1])) {
        if (qjs_to_bytes(cx, &bytes, argv[1]) != 0) {
            return JS_EXCEPTION;
        }

        key.start = bytes.start;
        key.length = bytes.length;

    } else {
        return JS_ThrowTypeError(cx,
                                 "key is not a string or Buffer-like object");
    }

    hmac = js_malloc(cx, sizeof(qjs_hmac_t));
    if (hmac == NULL) {
        if (key_is_string) {
            JS_FreeCString(cx, (const char *) key.start);
        }

        return JS_ThrowOutOfMemory(cx);
    }

    hmac->alg = alg;

    if (key.length > sizeof(key_buf)) {
        alg->init(&hmac->ctx);
        alg->update(&hmac->ctx, key.start, key.length);
        alg->final(digest, &hmac->ctx);

        memcpy(key_buf, digest, alg->size);
        memset(key_buf + alg->size, 0, sizeof(key_buf) - alg->size);

    } else {
        memcpy(key_buf, key.start, key.length);
        memset(key_buf + key.length, 0, sizeof(key_buf) - key.length);
    }

    if (key_is_string) {
        JS_FreeCString(cx, (const char *) key.start);
    }

    for (i = 0; i < 64; i++) {
        hmac->opad[i] = key_buf[i] ^ 0x5c;
    }

    for (i = 0; i < 64; i++) {
        key_buf[i] ^= 0x36;
    }

    alg->init(&hmac->ctx);
    alg->update(&hmac->ctx, key_buf, 64);

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_CRYPTO_HMAC);
    if (JS_IsException(obj)) {
        js_free(cx, hmac);
        return obj;
    }

    JS_SetOpaque(obj, hmac);

    return obj;
}


static void
qjs_hash_finalizer(JSRuntime *rt, JSValue val)
{
    qjs_digest_t  *dgst;

    dgst = JS_GetOpaque(val, QJS_CORE_CLASS_CRYPTO_HASH);
    if (dgst != NULL) {
        js_free_rt(rt, dgst);
    }
}


static void
qjs_hmac_finalizer(JSRuntime *rt, JSValue val)
{
    qjs_hmac_t  *hmac;

    hmac = JS_GetOpaque(val, QJS_CORE_CLASS_CRYPTO_HMAC);
    if (hmac != NULL) {
        js_free_rt(rt, hmac);
    }
}


static qjs_hash_alg_t *
qjs_crypto_algorithm(JSContext *cx, JSValueConst val)
{
    njs_str_t       name;
    qjs_hash_alg_t  *a, *alg;

    name.start = (u_char *) JS_ToCStringLen(cx, &name.length, val);
    if (name.start == NULL) {
        JS_ThrowTypeError(cx, "algorithm must be a string");
        return NULL;
    }

    alg = NULL;

    for (a = &qjs_hash_algorithms[0]; a->name.start != NULL; a++) {
        if (njs_strstr_eq(&name, &a->name)) {
            alg = a;
            break;
        }
    }

    JS_FreeCString(cx, (const char *) name.start);

    if (alg == NULL) {
        JS_ThrowTypeError(cx, "not supported algorithm");
    }

    return alg;
}


static qjs_crypto_enc_t *
qjs_crypto_encoding(JSContext *cx, JSValueConst val)
{
    njs_str_t         name;
    qjs_crypto_enc_t  *e, *enc;

    if (JS_IsNullOrUndefined(val)) {
        return &qjs_encodings[0];
    }

    name.start = (u_char *) JS_ToCStringLen(cx, &name.length, val);
    if (name.start == NULL) {
        return NULL;
    }

    enc = NULL;

    for (e = &qjs_encodings[1]; e->name.start != NULL; e++) {
        if (njs_strstr_eq(&name, &e->name)) {
            enc = e;
            break;
        }
    }

    JS_FreeCString(cx, (const char *) name.start);

    if (enc == NULL) {
        JS_ThrowTypeError(cx, "Unknown digest encoding");
    }

    return enc;
}


static JSValue
qjs_buffer_digest(JSContext *cx, const njs_str_t *src)
{
    return qjs_buffer_create(cx, src->start, src->length);
}


static int
qjs_crypto_module_init(JSContext *cx, JSModuleDef *m)
{
    JSValue crypto_obj;

    crypto_obj = JS_NewObject(cx);
    if (JS_IsException(crypto_obj)) {
        return -1;
    }

    JS_SetPropertyFunctionList(cx, crypto_obj, qjs_crypto_export,
                               njs_nitems(qjs_crypto_export));

    if (JS_SetModuleExport(cx, m, "default", crypto_obj) != 0) {
        return -1;
    }

    return JS_SetModuleExportList(cx, m, qjs_crypto_export,
                                  njs_nitems(qjs_crypto_export));
}


static JSModuleDef *
qjs_crypto_init(JSContext *cx, const char *module_name)
{
    JSValue      proto;
    JSModuleDef  *m;

    if (!JS_IsRegisteredClass(JS_GetRuntime(cx),
                              QJS_CORE_CLASS_CRYPTO_HASH))
    {
        if (JS_NewClass(JS_GetRuntime(cx), QJS_CORE_CLASS_CRYPTO_HASH,
                        &qjs_hash_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, qjs_hash_proto_proto,
                                   njs_nitems(qjs_hash_proto_proto));

        JS_SetClassProto(cx, QJS_CORE_CLASS_CRYPTO_HASH, proto);

        if (JS_NewClass(JS_GetRuntime(cx), QJS_CORE_CLASS_CRYPTO_HMAC,
                        &qjs_hmac_class) < 0)
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, qjs_hmac_proto_proto,
                                   njs_nitems(qjs_hmac_proto_proto));

        JS_SetClassProto(cx, QJS_CORE_CLASS_CRYPTO_HMAC, proto);
    }

    m = JS_NewCModule(cx, module_name, qjs_crypto_module_init);
    if (m == NULL) {
        return NULL;
    }

    if (JS_AddModuleExport(cx, m, "default") < 0) {
        return NULL;
    }

    if (JS_AddModuleExportList(cx, m, qjs_crypto_export,
                           njs_nitems(qjs_crypto_export)) != 0)
    {
        return NULL;
    }

    return m;
}
