
/*
 * Copyright (C) Vadim Zhestkov
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>
#include "njs_openssl.h"

typedef JSValue (*qjs_digest_encode)(JSContext *cx, const njs_str_t *src);


typedef struct {
    EVP_MD_CTX     *ctx;
} qjs_digest_t;

typedef struct {
    HMAC_CTX       *ctx;
} qjs_hmac_t;


typedef struct {
    njs_str_t          name;

    qjs_digest_encode  encode;
} qjs_crypto_enc_t;


static const EVP_MD *qjs_crypto_algorithm(JSContext *cx, JSValueConst val);
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
    JSValue       obj;
    qjs_digest_t  *dgst;
    const EVP_MD  *md;

    md = qjs_crypto_algorithm(cx, argv[0]);
    if (md == NULL) {
        return JS_EXCEPTION;
    }

    dgst = js_malloc(cx, sizeof(qjs_digest_t));
    if (dgst == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    dgst->ctx = njs_evp_md_ctx_new();
    if (dgst->ctx == NULL) {
        js_free(cx, dgst);
        return JS_ThrowOutOfMemory(cx);
    }

    if (EVP_DigestInit_ex(dgst->ctx, md, NULL) <= 0) {
        njs_evp_md_ctx_free(dgst->ctx);
        js_free(cx, dgst);
        JS_ThrowInternalError(cx, "EVP_DigestInit_ex() failed");
        return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_CRYPTO_HASH);
    if (JS_IsException(obj)) {
        njs_evp_md_ctx_free(dgst->ctx);
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
    qjs_hmac_t                   *hctx;
    qjs_bytes_t                  bytes;
    qjs_digest_t                 *dgst;
    const qjs_buffer_encoding_t  *enc;

    if (!hmac) {
        dgst = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_CRYPTO_HASH);
        if (dgst == NULL) {
            return JS_ThrowTypeError(cx, "\"this\" is not a hash object");
        }

        if (dgst->ctx == NULL) {
            return JS_ThrowTypeError(cx, "Digest already called");
        }

        hctx = NULL;

    } else {
        hctx = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_CRYPTO_HMAC);
        if (hctx == NULL) {
            return JS_ThrowTypeError(cx, "\"this\" is not a hmac object");
        }

        if (hctx->ctx == NULL) {
            return JS_ThrowTypeError(cx, "Digest already called");
        }

        dgst = NULL;
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
                js_free(cx, content.start);
                return JS_EXCEPTION;
            }

            JS_FreeCString(cx, (const char *) str.start);

            if (!hmac) {
                if (EVP_DigestUpdate(dgst->ctx, content.start, content.length)
                    <= 0)
                {
                    js_free(cx, content.start);
                    JS_ThrowInternalError(cx, "EVP_DigestUpdate() failed");
                    return JS_EXCEPTION;
                }

            } else {
                if (HMAC_Update(hctx->ctx, content.start, content.length) <= 0)
                {
                    js_free(cx, content.start);
                    JS_ThrowInternalError(cx, "HMAC_Update() failed");
                    return JS_EXCEPTION;
                }
            }

            js_free(cx, content.start);

        } else {
            if (!hmac) {
                if (EVP_DigestUpdate(dgst->ctx, str.start, str.length) <= 0) {
                    JS_FreeCString(cx, (const char *) str.start);
                    JS_ThrowInternalError(cx, "EVP_DigestUpdate() failed");
                    return JS_EXCEPTION;
                }

            } else {
                if (HMAC_Update(hctx->ctx, str.start, str.length) <= 0) {
                    JS_FreeCString(cx, (const char *) str.start);
                    JS_ThrowInternalError(cx, "HMAC_Update() failed");
                    return JS_EXCEPTION;
                }
            }

            JS_FreeCString(cx, (const char *) str.start);
        }

    } else if (qjs_is_typed_array(cx, argv[0])) {
        if (qjs_to_bytes(cx, &bytes, argv[0]) != 0) {
            return JS_EXCEPTION;
        }

        if (!hmac) {
            if (EVP_DigestUpdate(dgst->ctx, bytes.start, bytes.length) <= 0) {
                JS_ThrowInternalError(cx, "EVP_DigestUpdate() failed");
                return JS_EXCEPTION;
            }

        } else {
            if (HMAC_Update(hctx->ctx, bytes.start, bytes.length) <= 0) {
                JS_ThrowInternalError(cx, "HMAC_Update() failed");
                return JS_EXCEPTION;
            }
        }

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
    unsigned int      len;
    qjs_hmac_t        *hctx;
    qjs_digest_t      *dgst;
    qjs_crypto_enc_t  *enc;
    u_char            digest[EVP_MAX_MD_SIZE];

    if (!hmac) {
        dgst = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_CRYPTO_HASH);
        if (dgst == NULL) {
            return JS_ThrowTypeError(cx, "\"this\" is not a hash object");
        }

        if (dgst->ctx == NULL) {
            return JS_ThrowTypeError(cx, "Digest already called");
        }

        if (EVP_DigestFinal_ex(dgst->ctx, digest, &len) <= 0) {
            JS_ThrowInternalError(cx, "EVP_DigestFinal_ex() failed");
            return JS_EXCEPTION;
        }

        njs_evp_md_ctx_free(dgst->ctx);
        dgst->ctx = NULL;

    } else {
        hctx = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_CRYPTO_HMAC);
        if (hctx == NULL) {
            return JS_ThrowTypeError(cx, "\"this\" is not a hmac object");
        }

        if (hctx->ctx == NULL) {
            return JS_ThrowTypeError(cx, "Digest already called");
        }

        if (HMAC_Final(hctx->ctx, digest, &len) <= 0) {
            JS_ThrowInternalError(cx, "HMAC_Final() failed");
            return JS_EXCEPTION;
        }

        njs_hmac_ctx_free(hctx->ctx);
        hctx->ctx = NULL;
    }

    str.start = digest;
    str.length = len;

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

    if (dgst->ctx == NULL) {
        return JS_ThrowTypeError(cx, "Digest already called");
    }

    copy = js_malloc(cx, sizeof(qjs_digest_t));
    if (copy == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    copy->ctx = njs_evp_md_ctx_new();
    if (copy->ctx == NULL) {
        js_free(cx, copy);
        return JS_ThrowOutOfMemory(cx);
    }

    if (EVP_MD_CTX_copy_ex(copy->ctx, dgst->ctx) <= 0) {
        njs_evp_md_ctx_free(copy->ctx);
        js_free(cx, copy);
        JS_ThrowInternalError(cx, "EVP_MD_CTX_copy_ex() failed");
        return JS_EXCEPTION;
    }

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_CRYPTO_HASH);
    if (JS_IsException(obj)) {
        njs_evp_md_ctx_free(copy->ctx);
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
    JS_BOOL       key_is_string;
    JSValue       obj;
    njs_str_t     key;
    qjs_hmac_t    *hmac;
    qjs_bytes_t   bytes;
    const EVP_MD  *md;

    md = qjs_crypto_algorithm(cx, argv[0]);
    if (md == NULL) {
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

    hmac->ctx = njs_hmac_ctx_new();
    if (hmac->ctx == NULL) {
        js_free(cx, hmac);
        if (key_is_string) {
            JS_FreeCString(cx, (const char *) key.start);
        }

        return JS_ThrowOutOfMemory(cx);
    }

    if (HMAC_Init_ex(hmac->ctx, key.start, (int) key.length, md, NULL) <= 0) {
        njs_hmac_ctx_free(hmac->ctx);
        js_free(cx, hmac);
        if (key_is_string) {
            JS_FreeCString(cx, (const char *) key.start);
        }

        JS_ThrowInternalError(cx, "HMAC_Init_ex() failed");
        return JS_EXCEPTION;
    }

    if (key_is_string) {
        JS_FreeCString(cx, (const char *) key.start);
    }

    obj = JS_NewObjectClass(cx, QJS_CORE_CLASS_CRYPTO_HMAC);
    if (JS_IsException(obj)) {
        njs_hmac_ctx_free(hmac->ctx);
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
        if (dgst->ctx != NULL) {
            njs_evp_md_ctx_free(dgst->ctx);
        }

        js_free_rt(rt, dgst);
    }
}


static void
qjs_hmac_finalizer(JSRuntime *rt, JSValue val)
{
    qjs_hmac_t  *hmac;

    hmac = JS_GetOpaque(val, QJS_CORE_CLASS_CRYPTO_HMAC);
    if (hmac != NULL) {
        if (hmac->ctx != NULL) {
            njs_hmac_ctx_free(hmac->ctx);
        }

        js_free_rt(rt, hmac);
    }
}


static const EVP_MD *
qjs_crypto_algorithm(JSContext *cx, JSValueConst val)
{
    size_t        len;
    const char    *name;
    const EVP_MD  *md;

    name = JS_ToCStringLen(cx, &len, val);
    if (name == NULL) {
        JS_ThrowTypeError(cx, "algorithm must be a string");
        return NULL;
    }

    if (njs_strlen(name) != len) {
        JS_FreeCString(cx, name);
        JS_ThrowTypeError(cx, "not supported algorithm");
        return NULL;
    }

    md = EVP_get_digestbyname(name);

    JS_FreeCString(cx, name);

    if (md == NULL) {
        JS_ThrowTypeError(cx, "not supported algorithm");
        return NULL;
    }

    return md;
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
