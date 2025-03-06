
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */


#include <qjs.h>
#include <njs_sprintf.h>
#include "njs_openssl.h"

typedef enum {
    QJS_KEY_FORMAT_RAW          = 1 << 1,
    QJS_KEY_FORMAT_PKCS8        = 1 << 2,
    QJS_KEY_FORMAT_SPKI         = 1 << 3,
    QJS_KEY_FORMAT_JWK          = 1 << 4,
    QJS_KEY_FORMAT_UNKNOWN      = 1 << 5,
} qjs_webcrypto_key_format_t;


typedef enum {
    QJS_KEY_JWK_KTY_RSA,
    QJS_KEY_JWK_KTY_EC,
    QJS_KEY_JWK_KTY_OCT,
    QJS_KEY_JWK_KTY_UNKNOWN,
} qjs_webcrypto_jwk_kty_t;


typedef enum {
    QJS_KEY_USAGE_DECRYPT       = 1 << 1,
    QJS_KEY_USAGE_DERIVE_BITS   = 1 << 2,
    QJS_KEY_USAGE_DERIVE_KEY    = 1 << 3,
    QJS_KEY_USAGE_ENCRYPT       = 1 << 4,
    QJS_KEY_USAGE_GENERATE_KEY  = 1 << 5,
    QJS_KEY_USAGE_SIGN          = 1 << 6,
    QJS_KEY_USAGE_VERIFY        = 1 << 7,
    QJS_KEY_USAGE_WRAP_KEY      = 1 << 8,
    QJS_KEY_USAGE_UNSUPPORTED   = 1 << 9,
    QJS_KEY_USAGE_UNWRAP_KEY    = 1 << 10,
} qjs_webcrypto_key_usage_t;


typedef enum {
    QJS_ALGORITHM_RSASSA_PKCS1_v1_5 = 0,
    QJS_ALGORITHM_RSA_PSS,
    QJS_ALGORITHM_RSA_OAEP,
    QJS_ALGORITHM_HMAC,
    QJS_ALGORITHM_AES_GCM,
    QJS_ALGORITHM_AES_CTR,
    QJS_ALGORITHM_AES_CBC,
    QJS_ALGORITHM_ECDSA,
    QJS_ALGORITHM_ECDH,
    QJS_ALGORITHM_PBKDF2,
    QJS_ALGORITHM_HKDF,
    QJS_ALGORITHM_MAX,
} qjs_webcrypto_alg_t;


typedef enum {
    QJS_HASH_UNSET = 0,
    QJS_HASH_SHA1,
    QJS_HASH_SHA256,
    QJS_HASH_SHA384,
    QJS_HASH_SHA512,
    QJS_HASH_MAX,
} qjs_webcrypto_hash_t;


typedef struct {
    njs_str_t                  name;
    uintptr_t                  value;
} qjs_webcrypto_entry_t;


typedef struct {
    qjs_webcrypto_alg_t        type;
    unsigned                   usage;
    unsigned                   fmt;
    unsigned                   raw;
} qjs_webcrypto_algorithm_t;


typedef struct {
    qjs_webcrypto_algorithm_t  *alg;
    unsigned                   usage;
    int                        extractable;

    qjs_webcrypto_hash_t       hash;

    union {
        struct {
            EVP_PKEY          *pkey;
            int               privat;
            int               curve;
        } a;
        struct {
            njs_str_t         raw;
        } s;
    } u;

} qjs_webcrypto_key_t;


typedef int (*EVP_PKEY_cipher_init_t)(EVP_PKEY_CTX *ctx);
typedef int (*EVP_PKEY_cipher_t)(EVP_PKEY_CTX *ctx, unsigned char *out,
    size_t *outlen, const unsigned char *in, size_t inlen);

static JSValue qjs_webcrypto_cipher(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int encrypt);
static JSValue qjs_cipher_pkey(JSContext *cx, njs_str_t *data,
    qjs_webcrypto_key_t *key, int encrypt);
static JSValue qjs_cipher_aes_gcm(JSContext *cx, njs_str_t *data,
    qjs_webcrypto_key_t *key, JSValue options, int encrypt);
static JSValue qjs_cipher_aes_ctr(JSContext *cx, njs_str_t *data,
    qjs_webcrypto_key_t *key, JSValue options, int encrypt);
static JSValue qjs_cipher_aes_cbc(JSContext *cx, njs_str_t *data,
    qjs_webcrypto_key_t *key, JSValue options, int encrypt);
static JSValue qjs_webcrypto_derive(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int derive_key);
static JSValue qjs_webcrypto_digest(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_webcrypto_export_key(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_webcrypto_generate_key(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_webcrypto_import_key(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);
static JSValue qjs_webcrypto_sign(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int verify);

static JSValue qjs_webcrypto_key_algorithm(JSContext *cx,
    JSValueConst this_val);
static JSValue qjs_webcrypto_key_extractable(JSContext *cx,
    JSValueConst this_val);
static JSValue qjs_webcrypto_key_type(JSContext *cx, JSValueConst this_val);
static JSValue qjs_webcrypto_key_usages(JSContext *cx, JSValueConst this_val);

static JSValue qjs_get_random_values(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv);

static JSValue qjs_webcrypto_key_make(JSContext *cx,
  qjs_webcrypto_algorithm_t *alg, unsigned usage, int extractable);
static void qjs_webcrypto_key_finalizer(JSRuntime *rt, JSValue val);

static qjs_webcrypto_key_format_t qjs_key_format(JSContext *cx,
    JSValueConst value);
static qjs_webcrypto_jwk_kty_t qjs_jwk_kty(JSContext *cx, JSValueConst value);
static qjs_webcrypto_algorithm_t *qjs_key_algorithm(JSContext *cx, JSValue val);
static JSValue qjs_algorithm_curve(JSContext *cx, JSValue options, int *curve);
static JSValue qjs_algorithm_hash(JSContext *cx, JSValue options,
    qjs_webcrypto_hash_t *hash);
static const EVP_MD *qjs_algorithm_hash_digest(qjs_webcrypto_hash_t hash);
static njs_str_t *qjs_algorithm_curve_name(int curve);
static const char *qjs_format_string(qjs_webcrypto_key_format_t fmt);
static const char *qjs_algorithm_string(qjs_webcrypto_algorithm_t *algorithm);
static const char *qjs_algorithm_hash_name(qjs_webcrypto_hash_t hash);
static JSValue qjs_key_usage(JSContext *cx, JSValue value, unsigned *mask);
static JSValue qjs_key_ops(JSContext *cx, unsigned mask);
static JSValue qjs_webcrypto_result(JSContext *cx, JSValue result, int rc);
static void qjs_webcrypto_error(JSContext *cx, const char *fmt, ...);

static JSModuleDef *qjs_webcrypto_init(JSContext *cx, const char *name);


static qjs_webcrypto_entry_t qjs_webcrypto_alg[] = {

#define qjs_webcrypto_algorithm(type, usage, fmt, raw)                       \
    (uintptr_t) & (qjs_webcrypto_algorithm_t) { type, usage, fmt, raw }

    {
      njs_str("RSASSA-PKCS1-v1_5"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_RSASSA_PKCS1_v1_5,
                              QJS_KEY_USAGE_SIGN |
                              QJS_KEY_USAGE_VERIFY |
                              QJS_KEY_USAGE_GENERATE_KEY,
                              QJS_KEY_FORMAT_PKCS8 |
                              QJS_KEY_FORMAT_SPKI |
                              QJS_KEY_FORMAT_JWK,
                              0)
    },

    {
      njs_str("RSA-PSS"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_RSA_PSS,
                              QJS_KEY_USAGE_SIGN |
                              QJS_KEY_USAGE_VERIFY |
                              QJS_KEY_USAGE_GENERATE_KEY,
                              QJS_KEY_FORMAT_PKCS8 |
                              QJS_KEY_FORMAT_SPKI |
                              QJS_KEY_FORMAT_JWK,
                              0)
    },

    {
      njs_str("RSA-OAEP"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_RSA_OAEP,
                              QJS_KEY_USAGE_ENCRYPT |
                              QJS_KEY_USAGE_DECRYPT |
                              QJS_KEY_USAGE_WRAP_KEY |
                              QJS_KEY_USAGE_UNWRAP_KEY |
                              QJS_KEY_USAGE_GENERATE_KEY,
                              QJS_KEY_FORMAT_PKCS8 |
                              QJS_KEY_FORMAT_SPKI |
                              QJS_KEY_FORMAT_JWK,
                              0)
    },

    {
      njs_str("HMAC"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_HMAC,
                              QJS_KEY_USAGE_GENERATE_KEY |
                              QJS_KEY_USAGE_SIGN |
                              QJS_KEY_USAGE_VERIFY,
                              QJS_KEY_FORMAT_RAW |
                              QJS_KEY_FORMAT_JWK,
                              1)
    },

    {
      njs_str("AES-GCM"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_AES_GCM,
                              QJS_KEY_USAGE_ENCRYPT |
                              QJS_KEY_USAGE_DECRYPT |
                              QJS_KEY_USAGE_WRAP_KEY |
                              QJS_KEY_USAGE_UNWRAP_KEY |
                              QJS_KEY_USAGE_GENERATE_KEY,
                              QJS_KEY_FORMAT_RAW |
                              QJS_KEY_FORMAT_JWK,
                              1)
    },

    {
      njs_str("AES-CTR"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_AES_CTR,
                              QJS_KEY_USAGE_ENCRYPT |
                              QJS_KEY_USAGE_DECRYPT |
                              QJS_KEY_USAGE_WRAP_KEY |
                              QJS_KEY_USAGE_UNWRAP_KEY |
                              QJS_KEY_USAGE_GENERATE_KEY,
                              QJS_KEY_FORMAT_RAW |
                              QJS_KEY_FORMAT_JWK,
                              1)
    },

    {
      njs_str("AES-CBC"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_AES_CBC,
                              QJS_KEY_USAGE_ENCRYPT |
                              QJS_KEY_USAGE_DECRYPT |
                              QJS_KEY_USAGE_WRAP_KEY |
                              QJS_KEY_USAGE_UNWRAP_KEY |
                              QJS_KEY_USAGE_GENERATE_KEY,
                              QJS_KEY_FORMAT_RAW |
                              QJS_KEY_FORMAT_JWK,
                              1)
    },

    {
      njs_str("ECDSA"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_ECDSA,
                              QJS_KEY_USAGE_SIGN |
                              QJS_KEY_USAGE_VERIFY |
                              QJS_KEY_USAGE_GENERATE_KEY,
                              QJS_KEY_FORMAT_PKCS8 |
                              QJS_KEY_FORMAT_SPKI |
                              QJS_KEY_FORMAT_RAW |
                              QJS_KEY_FORMAT_JWK,
                              0)
    },

    {
      njs_str("ECDH"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_ECDH,
                              QJS_KEY_USAGE_DERIVE_KEY |
                              QJS_KEY_USAGE_DERIVE_BITS |
                              QJS_KEY_USAGE_GENERATE_KEY |
                              QJS_KEY_USAGE_UNSUPPORTED,
                              QJS_KEY_FORMAT_UNKNOWN,
                              0)
    },

    {
      njs_str("PBKDF2"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_PBKDF2,
                              QJS_KEY_USAGE_DERIVE_KEY |
                              QJS_KEY_USAGE_DERIVE_BITS,
                              QJS_KEY_FORMAT_RAW,
                              1)
    },

    {
      njs_str("HKDF"),
      qjs_webcrypto_algorithm(QJS_ALGORITHM_HKDF,
                              QJS_KEY_USAGE_DERIVE_KEY |
                              QJS_KEY_USAGE_DERIVE_BITS,
                              QJS_KEY_FORMAT_RAW,
                              1)
    },

    {
        njs_null_str,
        0
    }
};


static qjs_webcrypto_entry_t qjs_webcrypto_hash[] = {
    { njs_str("SHA-256"), QJS_HASH_SHA256 },
    { njs_str("SHA-384"), QJS_HASH_SHA384 },
    { njs_str("SHA-512"), QJS_HASH_SHA512 },
    { njs_str("SHA-1"), QJS_HASH_SHA1 },
    { njs_null_str, 0 }
};


static qjs_webcrypto_entry_t qjs_webcrypto_curve[] = {
    { njs_str("P-256"), NID_X9_62_prime256v1 },
    { njs_str("P-384"), NID_secp384r1 },
    { njs_str("P-521"), NID_secp521r1 },
    { njs_null_str, 0 }
};


static qjs_webcrypto_entry_t qjs_webcrypto_format[] = {
    { njs_str("raw"), QJS_KEY_FORMAT_RAW },
    { njs_str("pkcs8"), QJS_KEY_FORMAT_PKCS8 },
    { njs_str("spki"), QJS_KEY_FORMAT_SPKI },
    { njs_str("jwk"), QJS_KEY_FORMAT_JWK },
    { njs_null_str, QJS_KEY_FORMAT_UNKNOWN }
};


static qjs_webcrypto_entry_t qjs_webcrypto_jwk_kty[] = {
    { njs_str("RSA"), QJS_KEY_JWK_KTY_RSA },
    { njs_str("EC"), QJS_KEY_JWK_KTY_EC },
    { njs_str("oct"), QJS_KEY_JWK_KTY_OCT },
    { njs_null_str, QJS_KEY_JWK_KTY_UNKNOWN }
};


static qjs_webcrypto_entry_t qjs_webcrypto_usage[] = {
    { njs_str("decrypt"), QJS_KEY_USAGE_DECRYPT },
    { njs_str("deriveBits"), QJS_KEY_USAGE_DERIVE_BITS },
    { njs_str("deriveKey"), QJS_KEY_USAGE_DERIVE_KEY },
    { njs_str("encrypt"), QJS_KEY_USAGE_ENCRYPT },
    { njs_str("sign"), QJS_KEY_USAGE_SIGN },
    { njs_str("unwrapKey"), QJS_KEY_USAGE_UNWRAP_KEY },
    { njs_str("verify"), QJS_KEY_USAGE_VERIFY },
    { njs_str("wrapKey"), QJS_KEY_USAGE_WRAP_KEY },
    { njs_null_str, 0 }
};


static qjs_webcrypto_entry_t qjs_webcrypto_alg_hash[] = {
    { njs_str("RS1"), QJS_HASH_SHA1 },
    { njs_str("RS256"), QJS_HASH_SHA256 },
    { njs_str("RS384"), QJS_HASH_SHA384 },
    { njs_str("RS512"), QJS_HASH_SHA512 },
    { njs_str("PS1"), QJS_HASH_SHA1 },
    { njs_str("PS256"), QJS_HASH_SHA256 },
    { njs_str("PS384"), QJS_HASH_SHA384 },
    { njs_str("PS512"), QJS_HASH_SHA512 },
    { njs_str("RSA-OAEP"), QJS_HASH_SHA1 },
    { njs_str("RSA-OAEP-256"), QJS_HASH_SHA256 },
    { njs_str("RSA-OAEP-384"), QJS_HASH_SHA384 },
    { njs_str("RSA-OAEP-512"), QJS_HASH_SHA512 },
    { njs_null_str, 0 }
};


static njs_str_t
    qjs_webcrypto_alg_name[QJS_ALGORITHM_HMAC + 1][QJS_HASH_MAX] = {
    {
        njs_null_str,
        njs_str("RS1"),
        njs_str("RS256"),
        njs_str("RS384"),
        njs_str("RS512"),
    },

    {
        njs_null_str,
        njs_str("PS1"),
        njs_str("PS256"),
        njs_str("PS384"),
        njs_str("PS512"),
    },

    {
        njs_null_str,
        njs_str("RSA-OAEP"),
        njs_str("RSA-OAEP-256"),
        njs_str("RSA-OAEP-384"),
        njs_str("RSA-OAEP-512"),
    },

    {
        njs_null_str,
        njs_str("HS1"),
        njs_str("HS256"),
        njs_str("HS384"),
        njs_str("HS512"),
    },
};

static njs_str_t qjs_webcrypto_alg_aes_name[3][3 + 1] = {
    {
        njs_str("A128GCM"),
        njs_str("A192GCM"),
        njs_str("A256GCM"),
        njs_null_str,
    },

    {
        njs_str("A128CTR"),
        njs_str("A192CTR"),
        njs_str("A256CTR"),
        njs_null_str,
    },

    {
        njs_str("A128CBC"),
        njs_str("A192CBC"),
        njs_str("A256CBC"),
        njs_null_str,
    },
};


static const JSCFunctionListEntry qjs_webcrypto_subtle[] = {
    JS_CFUNC_DEF("importKey", 5, qjs_webcrypto_import_key),
    JS_CFUNC_MAGIC_DEF("decrypt", 4, qjs_webcrypto_cipher, 0),
    JS_CFUNC_MAGIC_DEF("deriveBits", 4, qjs_webcrypto_derive, 0),
    JS_CFUNC_MAGIC_DEF("deriveKey", 4, qjs_webcrypto_derive, 1),
    JS_CFUNC_DEF("digest", 3, qjs_webcrypto_digest),
    JS_CFUNC_MAGIC_DEF("encrypt", 4, qjs_webcrypto_cipher, 1),
    JS_CFUNC_DEF("exportKey", 3, qjs_webcrypto_export_key),
    JS_CFUNC_DEF("generateKey", 3, qjs_webcrypto_generate_key),
    JS_CFUNC_MAGIC_DEF("sign", 4, qjs_webcrypto_sign, 0),
    JS_CFUNC_MAGIC_DEF("verify", 4, qjs_webcrypto_sign, 1),
};


static const JSCFunctionListEntry qjs_webcrypto_key_proto[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "CryptoKey",
                       JS_PROP_CONFIGURABLE),
    JS_CGETSET_DEF("algorithm", qjs_webcrypto_key_algorithm, NULL),
    JS_CGETSET_DEF("extractable", qjs_webcrypto_key_extractable, NULL),
    JS_CGETSET_DEF("type", qjs_webcrypto_key_type, NULL),
    JS_CGETSET_DEF("usages", qjs_webcrypto_key_usages, NULL),
};


static const JSCFunctionListEntry qjs_webcrypto_export[] = {
    JS_CFUNC_DEF("getRandomValues", 1, qjs_get_random_values),
    JS_OBJECT_DEF("subtle",
                  qjs_webcrypto_subtle,
                  njs_nitems(qjs_webcrypto_subtle),
                  JS_PROP_CONFIGURABLE),
};


static JSClassDef qjs_webcrypto_key_class = {
    "WebCryptoKey",
    .finalizer = qjs_webcrypto_key_finalizer,
};


qjs_module_t  qjs_webcrypto_module = {
    .name = "webcrypto",
    .init = qjs_webcrypto_init,
};


static JSValue
qjs_webcrypto_cipher(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv, int encrypt)
{
    unsigned                   mask;
    JSValue                    ret, options;
    njs_str_t                  data;
    qjs_webcrypto_key_t        *key;
    qjs_webcrypto_algorithm_t  *alg;

    options = argv[0];
    alg = qjs_key_algorithm(cx, options);
    if (alg == NULL) {
        goto fail;
    }

    key = JS_GetOpaque(argv[1], QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    if (key == NULL) {
        JS_ThrowTypeError(cx, "key is not a CryptoKey object");
        goto fail;
    }

    mask = encrypt ? QJS_KEY_USAGE_ENCRYPT : QJS_KEY_USAGE_DECRYPT;
    if ((key->usage & mask) != mask) {
        JS_ThrowTypeError(cx, "key does not support %s operation",
                          encrypt ? "encrypt" : "decrypt");
        goto fail;
    }

    if (key->alg != alg) {
        JS_ThrowTypeError(cx, "cannot %s use \"%s\" with \"%s\" key",
                          encrypt ? "encrypt" : "decrypt",
                          qjs_algorithm_string(key->alg),
                          qjs_algorithm_string(alg));
        goto fail;
    }

    ret = qjs_typed_array_data(cx, argv[2], &data);
    if (JS_IsException(ret)) {
        return ret;
    }

    switch (alg->type) {
    case QJS_ALGORITHM_RSA_OAEP:
        ret = qjs_cipher_pkey(cx, &data, key, encrypt);
        if (JS_IsException(ret)) {
            goto fail;
        }

        break;

    case QJS_ALGORITHM_AES_GCM:
        ret = qjs_cipher_aes_gcm(cx, &data, key, options, encrypt);
        if (JS_IsException(ret)) {
            goto fail;
        }

        break;

    case QJS_ALGORITHM_AES_CTR:
        ret = qjs_cipher_aes_ctr(cx, &data, key, options, encrypt);
        if (JS_IsException(ret)) {
            goto fail;
        }

        break;

    case QJS_ALGORITHM_AES_CBC:
    default:
        ret = qjs_cipher_aes_cbc(cx, &data, key, options, encrypt);
        if (JS_IsException(ret)) {
            goto fail;
        }
    }

    return qjs_webcrypto_result(cx, ret, 0);

fail:

    return qjs_webcrypto_result(cx, JS_UNDEFINED, -1);
}


static JSValue
qjs_cipher_pkey(JSContext *cx, njs_str_t *data, qjs_webcrypto_key_t *key,
    int encrypt)
{
    int                     rc;
    u_char                  *dst;
    size_t                  outlen;
    JSValue                 ret;
    const EVP_MD            *md;
    EVP_PKEY_CTX            *ctx;
    EVP_PKEY_cipher_t       cipher;
    EVP_PKEY_cipher_init_t  init;

    ctx = EVP_PKEY_CTX_new(key->u.a.pkey, NULL);
    if (ctx == NULL) {
        qjs_webcrypto_error(cx, "EVP_PKEY_CTX_new() failed");
        return JS_EXCEPTION;
    }

    if (encrypt) {
        init = EVP_PKEY_encrypt_init;
        cipher = EVP_PKEY_encrypt;

    } else {
        init = EVP_PKEY_decrypt_init;
        cipher = EVP_PKEY_decrypt;
    }

    rc = init(ctx);
    if (rc <= 0) {
        qjs_webcrypto_error(cx, "EVP_PKEY_%scrypt_init() failed",
                            encrypt ? "en" : "de");
        ret = JS_EXCEPTION;
        goto fail;
    }

    md = qjs_algorithm_hash_digest(key->hash);

    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING);
    EVP_PKEY_CTX_set_signature_md(ctx, md);
    EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, md);

    rc = cipher(ctx, NULL, &outlen, data->start, data->length);
    if (rc <= 0) {
        qjs_webcrypto_error(cx, "EVP_PKEY_%scrypt() failed",
                            encrypt ? "en" : "de");
        ret = JS_EXCEPTION;
        goto fail;
    }

    dst = js_malloc(cx, outlen);
    if (dst == NULL) {
        JS_ThrowOutOfMemory(cx);
        ret = JS_EXCEPTION;
        goto fail;
    }

    rc = cipher(ctx, dst, &outlen, data->start, data->length);
    if (rc <= 0) {
        qjs_webcrypto_error(cx, "EVP_PKEY_%scrypt() failed",
                            encrypt ? "en" : "de");
        js_free(cx, dst);
        ret = JS_EXCEPTION;
        goto fail;
    }

    ret = qjs_new_array_buffer(cx, dst, outlen);

fail:

    EVP_PKEY_CTX_free(ctx);

    return ret;
}


static JSValue
qjs_cipher_aes_gcm(JSContext *cx, njs_str_t *data, qjs_webcrypto_key_t *key,
    JSValue options, int encrypt)
{
    int               len, outlen, dstlen;
    u_char            *dst, *p;
    JSValue           ret, value;
    int64_t           taglen;
    njs_str_t         iv, aad;
    EVP_CIPHER_CTX    *ctx;
    const EVP_CIPHER  *cipher;

    switch (key->u.s.raw.length) {
    case 16:
        cipher = EVP_aes_128_gcm();
        break;

    case 24:
        cipher = EVP_aes_192_gcm();
        break;

    case 32:
        cipher = EVP_aes_256_gcm();
        break;

    default:
        JS_ThrowTypeError(cx, "AES-GCM invalid key length");
        return JS_EXCEPTION;
    }

    value = JS_GetPropertyStr(cx, options, "iv");
    if (JS_IsException(value)) {
        return JS_EXCEPTION;
    }

    if (JS_IsUndefined(value)) {
        JS_ThrowTypeError(cx, "AES-GCM algorithm.iv is not provided");
        return JS_EXCEPTION;
    }

    ret = qjs_typed_array_data(cx, value, &iv);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;
    }

    JS_FreeValue(cx, value);

    taglen = 128;

    value = JS_GetPropertyStr(cx, options, "tagLength");
    if (JS_IsException(value)) {
        return JS_EXCEPTION;
    }

    if (!JS_IsUndefined(value)) {
        if (JS_ToInt64(cx, &taglen, value) < 0) {
            return JS_EXCEPTION;
        }
    }

    if (taglen != 32 && taglen != 64 && taglen != 96 && taglen != 104
        && taglen != 112 && taglen != 120 && taglen != 128)
    {
        JS_ThrowTypeError(cx, "AES-GCM invalid tagLength");
        return JS_EXCEPTION;
    }

    taglen /= 8;

    if (!encrypt && data->length < (size_t) taglen) {
        JS_ThrowTypeError(cx, "AES-GCM data is too short");
        return JS_EXCEPTION;
    }

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        qjs_webcrypto_error(cx, "EVP_CIPHER_CTX_new() failed");
        return JS_EXCEPTION;
    }

    if (EVP_CipherInit_ex(ctx, cipher, NULL, NULL, NULL, encrypt) <= 0) {
        qjs_webcrypto_error(cx, "EVP_%sInit_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        goto fail;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.length, NULL) <= 0) {
        qjs_webcrypto_error(cx, "EVP_CIPHER_CTX_ctrl() failed");
        goto fail;
    }

    if (EVP_CipherInit_ex(ctx, NULL, NULL, key->u.s.raw.start, iv.start,
                          encrypt) <= 0)
    {
        qjs_webcrypto_error(cx, "EVP_%sInit_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        goto fail;
    }

    if (!encrypt) {
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, taglen,
                                &data->start[data->length - taglen]) <= 0)
        {
            qjs_webcrypto_error(cx, "EVP_CIPHER_CTX_ctrl() failed");
            goto fail;
        }
    }

    aad.length = 0;

    value = JS_GetPropertyStr(cx, options, "additionalData");
    if (JS_IsException(value)) {
        goto fail;
    }

    if (!JS_IsUndefined(value)) {
        ret = qjs_typed_array_data(cx, value, &aad);
        if (JS_IsException(ret)) {
            goto fail;
        }
    }

    JS_FreeValue(cx, value);

    if (aad.length != 0) {
        if (EVP_CipherUpdate(ctx, NULL, &outlen, aad.start, aad.length) <= 0) {
            qjs_webcrypto_error(cx, "EVP_%sUpdate() failed",
                                encrypt ? "Encrypt" : "Decrypt");
            goto fail;
        }
    }

    dstlen = data->length + EVP_CIPHER_CTX_block_size(ctx) + taglen;
    dst = js_malloc(cx, dstlen);
    if (dst == NULL) {
        JS_ThrowOutOfMemory(cx);
        goto fail;
    }

    if (EVP_CipherUpdate(ctx, dst, &outlen, data->start,
                         data->length - (encrypt ? 0 : taglen)) <= 0)
    {
        qjs_webcrypto_error(cx, "EVP_%sUpdate() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        js_free(cx, dst);
        goto fail;
    }

    p = &dst[outlen];
    len = EVP_CIPHER_CTX_block_size(ctx);

    if (EVP_CipherFinal_ex(ctx, p, &len) <= 0) {
        qjs_webcrypto_error(cx, "EVP_%sFinal_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        js_free(cx, dst);
        goto fail;
    }

    outlen += len;
    p += len;

    if (encrypt) {
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, taglen, p) <= 0) {
            qjs_webcrypto_error(cx, "EVP_CIPHER_CTX_ctrl() failed");
            js_free(cx, dst);
            goto fail;
        }

        outlen += taglen;
    }

    ret = qjs_new_array_buffer(cx, dst, outlen);

    EVP_CIPHER_CTX_free(ctx);

    return ret;

fail:

    EVP_CIPHER_CTX_free(ctx);

    return JS_EXCEPTION;
}


static int
qjs_cipher_aes_ctr128(JSContext *cx, const EVP_CIPHER *cipher, u_char *key,
    u_char *data, size_t dlen, u_char *counter, u_char *dst, int *olen,
    int encrypt)
{
    int             len, outlen;
    int             ret;
    EVP_CIPHER_CTX  *ctx;

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        qjs_webcrypto_error(cx, "EVP_CIPHER_CTX_new() failed");
        return -1;
    }

    ret = EVP_CipherInit_ex(ctx, cipher, NULL, key, counter, encrypt);
    if (ret <= 0) {
        qjs_webcrypto_error(cx, "EVP_%sInit_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = -1;
        goto fail;
    }

    ret = EVP_CipherUpdate(ctx, dst, &outlen, data, dlen);
    if (ret <= 0) {
        qjs_webcrypto_error(cx, "EVP_%sUpdate() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = -1;
        goto fail;
    }

    ret = EVP_CipherFinal_ex(ctx, &dst[outlen], &len);
    if (ret <= 0) {
        qjs_webcrypto_error(cx, "EVP_%sFinal_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = -1;
        goto fail;
    }

    outlen += len;
    *olen = outlen;

    ret = 0;

fail:

    EVP_CIPHER_CTX_free(ctx);

    return ret;
}


static BIGNUM *
qjs_bn_counter128(njs_str_t *ctr, unsigned bits)
{
    unsigned  remainder, bytes;
    uint8_t   buf[16];

    remainder = bits % 8;

    if (remainder == 0) {
        bytes = bits / 8;

        return BN_bin2bn(&ctr->start[ctr->length - bytes], bytes, NULL);
    }

    bytes = (bits + 7) / 8;

    memcpy(buf, &ctr->start[ctr->length - bytes], bytes);

    buf[0] &= ~(0xFF << remainder);

    return BN_bin2bn(buf, bytes, NULL);
}


njs_inline unsigned
qjs_ceiling_div(unsigned dend, unsigned dsor)
{
    return (dsor == 0) ? 0 : 1 + (dend - 1) / dsor;
}


static void
qjs_counter128_reset(u_char *src, u_char *dst, unsigned bits)
{
    size_t    index;
    unsigned  remainder, bytes;

    bytes = bits / 8;
    remainder = bits % 8;

    memcpy(dst, src, 16);

    index = 16 - bytes;

    memset(&dst[index], 0, bytes);

    if (remainder) {
        dst[index - 1] &= 0xff << remainder;
    }
}


static JSValue
qjs_cipher_aes_ctr(JSContext *cx, njs_str_t *data, qjs_webcrypto_key_t *key,
    JSValue options, int encrypt)
{
    int               len, len2;
    u_char            *dst;
    BIGNUM            *total, *blocks, *left, *ctr;
    size_t            size1;
    JSValue           ret, value;
    int64_t           length;
    njs_str_t         iv;
    const EVP_CIPHER  *cipher;
    u_char            iv2[16];

    switch (key->u.s.raw.length) {
    case 16:
        cipher = EVP_aes_128_ctr();
        break;

    case 24:
        cipher = EVP_aes_192_ctr();
        break;

    case 32:
        cipher = EVP_aes_256_ctr();
        break;

    default:
        JS_ThrowTypeError(cx, "AES-CTR invalid key length");
        return JS_EXCEPTION;
    }

    value = JS_GetPropertyStr(cx, options, "counter");
    if (JS_IsException(value)) {
        return JS_EXCEPTION;
    }

    if (JS_IsUndefined(value)) {
        JS_ThrowTypeError(cx, "AES-CTR algorithm.counter is not provided");
        return JS_EXCEPTION;
    }

    ret = qjs_typed_array_data(cx, value, &iv);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;
    }

    JS_FreeValue(cx, value);

    if (iv.length != 16) {
        JS_ThrowTypeError(cx, "AES-CTR algorithm.counter must be 16 bytes "
                          "long");
        return JS_EXCEPTION;
    }

    value = JS_GetPropertyStr(cx, options, "length");
    if (JS_IsException(value)) {
        return JS_EXCEPTION;
    }

    if (JS_IsUndefined(value)) {
        JS_ThrowTypeError(cx, "AES-CTR algorithm.length is not provided");
        return JS_EXCEPTION;
    }

    if (JS_ToInt64(cx, &length, value) < 0) {
        return JS_EXCEPTION;
    }

    if (length == 0 || length > 128) {
        JS_ThrowTypeError(cx, "AES-CTR algorithm.length must be between "
                          "1 and 128");
        return JS_EXCEPTION;
    }

    ctr = NULL;
    blocks = NULL;
    left = NULL;

    total = BN_new();
    if (total == NULL) {
        qjs_webcrypto_error(cx, "BN_new() failed");
        return JS_EXCEPTION;
    }

    if (BN_lshift(total, BN_value_one(), length) != 1) {
        qjs_webcrypto_error(cx, "BN_lshift() failed");
        ret = JS_EXCEPTION;
        goto fail;
    }

    ctr = qjs_bn_counter128(&iv, length);
    if (ctr == NULL) {
        qjs_webcrypto_error(cx, "BN_bin2bn() failed");
        ret = JS_EXCEPTION;
        goto fail;
    }

    blocks = BN_new();
    if (blocks == NULL) {
        qjs_webcrypto_error(cx, "BN_new() failed");
        return JS_EXCEPTION;
    }

    if (BN_set_word(blocks, qjs_ceiling_div(data->length, AES_BLOCK_SIZE))
        != 1)
    {
        qjs_webcrypto_error(cx, "BN_set_word() failed");
        ret = JS_EXCEPTION;
        goto fail;
    }

    if (BN_cmp(blocks, total) > 0) {
        JS_ThrowTypeError(cx, "AES-CTR repeated counter");
        ret = JS_EXCEPTION;
        goto fail;
    }

    left = BN_new();
    if (left == NULL) {
        qjs_webcrypto_error(cx, "BN_new() failed");
        return JS_EXCEPTION;
    }

    if (BN_sub(left, total, ctr) != 1) {
        qjs_webcrypto_error(cx, "BN_sub() failed");
        ret = JS_EXCEPTION;
        goto fail;
    }

    dst = js_malloc(cx, data->length + EVP_MAX_BLOCK_LENGTH);
    if (dst == NULL) {
        JS_ThrowOutOfMemory(cx);
        ret = JS_EXCEPTION;
        goto fail;
    }

    if (BN_cmp(left, blocks) >= 0) {
        /*
         * Doing a single run if a counter is not wrapped-around
         * during the ciphering.
         */
        if (qjs_cipher_aes_ctr128(cx, cipher, key->u.s.raw.start,
                                  data->start, data->length, iv.start, dst,
                                  &len, encrypt) < 0)
        {
            js_free(cx, dst);
            ret = JS_EXCEPTION;
            goto fail;
        }

        goto done;
    }

    /*
     * Otherwise splitting ciphering into two parts:
     *  Until the wrapping moment
     *  After the resetting counter to zero.
     */

    size1 = BN_get_word(left) * AES_BLOCK_SIZE;

    if (qjs_cipher_aes_ctr128(cx, cipher, key->u.s.raw.start, data->start,
                              size1, iv.start, dst, &len, encrypt) < 0)
    {
        js_free(cx, dst);
        ret = JS_EXCEPTION;
        goto fail;
    }

    qjs_counter128_reset(iv.start, iv2, length);

    if (qjs_cipher_aes_ctr128(cx, cipher, key->u.s.raw.start,
                              &data->start[size1], data->length - size1,
                              iv2, &dst[size1], &len2, encrypt) < 0)
    {
        js_free(cx, dst);
        ret = JS_EXCEPTION;
        goto fail;
    }

    len += len2;

done:

    ret = qjs_new_array_buffer(cx, dst, len);

fail:

    BN_free(total);

    if (ctr != NULL) {
        BN_free(ctr);
    }

    if (blocks != NULL) {
        BN_free(blocks);
    }

    if (left != NULL) {
        BN_free(left);
    }

    return ret;
}


static JSValue
qjs_cipher_aes_cbc(JSContext *cx, njs_str_t *data, qjs_webcrypto_key_t *key,
    JSValue options, int encrypt)
{
    int               rc, olen_max, olen, olen2;
    u_char            *dst;
    JSValue           ret, value;
    unsigned          remainder;
    njs_str_t         iv;
    EVP_CIPHER_CTX    *ctx;
    const EVP_CIPHER  *cipher;

    switch (key->u.s.raw.length) {
    case 16:
        cipher = EVP_aes_128_cbc();
        break;

    case 24:
        cipher = EVP_aes_192_cbc();
        break;

    case 32:
        cipher = EVP_aes_256_cbc();
        break;

    default:
        JS_ThrowTypeError(cx, "AES-CBC invalid key length");
        return JS_EXCEPTION;
    }

    value = JS_GetPropertyStr(cx, options, "iv");
    if (JS_IsException(value)) {
        return JS_EXCEPTION;
    }

    if (JS_IsUndefined(value)) {
        JS_ThrowTypeError(cx, "AES-CBC algorithm.iv is not provided");
        return JS_EXCEPTION;
    }

    ret = qjs_typed_array_data(cx, value, &iv);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;
    }

    JS_FreeValue(cx, value);

    if (iv.length != 16) {
        JS_ThrowTypeError(cx, "AES-CBC algorithm.iv must be 16 bytes long");
        return JS_EXCEPTION;
    }

    olen_max = data->length + AES_BLOCK_SIZE - 1;
    remainder = olen_max % AES_BLOCK_SIZE;

    if (remainder != 0) {
        olen_max += AES_BLOCK_SIZE - remainder;
    }

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        qjs_webcrypto_error(cx, "EVP_CIPHER_CTX_new() failed");
        return JS_EXCEPTION;
    }

    rc = EVP_CipherInit_ex(ctx, cipher, NULL, key->u.s.raw.start, iv.start,
                            encrypt);
    if (rc <= 0) {
        qjs_webcrypto_error(cx, "EVP_%sInit_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = JS_EXCEPTION;
        goto fail;
    }

    dst = js_malloc(cx, olen_max);
    if (dst == NULL) {
        JS_ThrowOutOfMemory(cx);
        ret = JS_EXCEPTION;
        goto fail;
    }

    rc = EVP_CipherUpdate(ctx, dst, &olen, data->start, data->length);
    if (rc <= 0) {
        qjs_webcrypto_error(cx, "EVP_%sUpdate() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        js_free(cx, dst);
        ret = JS_EXCEPTION;
        goto fail;
    }

    rc = EVP_CipherFinal_ex(ctx, &dst[olen], &olen2);
    if (rc <= 0) {
        qjs_webcrypto_error(cx, "EVP_%sFinal_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        js_free(cx, dst);
        ret = JS_EXCEPTION;
        goto fail;
    }

    olen += olen2;

    ret = qjs_new_array_buffer(cx, dst, olen);

fail:

    EVP_CIPHER_CTX_free(ctx);

    return ret;
}


static JSValue
qjs_export_base64url_bignum(JSContext *cx, const BIGNUM *v, size_t size)
{
    njs_str_t  src;
    u_char     buf[512];

    if (size == 0) {
        size = BN_num_bytes(v);
    }

    if (njs_bn_bn2binpad(v, &buf[0], size) <= 0) {
        JS_ThrowInternalError(cx, "njs_bn_bn2binpad() failed");
        return JS_EXCEPTION;
    }

    src.start = buf;
    src.length = size;

    return qjs_string_base64url(cx, &src);
}


static int
qjs_base64url_bignum_set(JSContext *cx, JSValue jwk, const char *key,
    const BIGNUM *v, size_t size)
{
    JSValue  value;

    value = qjs_export_base64url_bignum(cx, v, size);
    if (JS_IsException(value)) {
        return -1;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, key, value, JS_PROP_C_W_E) < 0) {
        JS_FreeValue(cx, value);
        return -1;
    }

    return 0;
}


static JSValue
qjs_export_jwk_rsa(JSContext *cx, qjs_webcrypto_key_t *key)
{
    JSValue       jwk, alg;
    njs_str_t     *nm;
    const RSA     *rsa;
    const BIGNUM  *n_bn, *e_bn, *d_bn, *p_bn, *q_bn, *dp_bn, *dq_bn, *qi_bn;

    rsa = njs_pkey_get_rsa_key(key->u.a.pkey);

    njs_rsa_get0_key(rsa, &n_bn, &e_bn, &d_bn);

    jwk = JS_NewObject(cx);
    if (JS_IsException(jwk)) {
        return JS_EXCEPTION;
    }

    if (qjs_base64url_bignum_set(cx, jwk, "n", n_bn, 0) < 0) {
        goto fail;
    }

    if (qjs_base64url_bignum_set(cx, jwk, "e", e_bn, 0) < 0) {
        goto fail;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "kty", JS_NewString(cx, "RSA"),
                                  JS_PROP_C_W_E) < 0)
    {
        goto fail;
    }

    if (key->u.a.privat) {
        njs_rsa_get0_factors(rsa, &p_bn, &q_bn);
        njs_rsa_get0_ctr_params(rsa, &dp_bn, &dq_bn, &qi_bn);

        if (qjs_base64url_bignum_set(cx, jwk, "d", d_bn, 0) < 0) {
            goto fail;
        }

        if (qjs_base64url_bignum_set(cx, jwk, "p", p_bn, 0) < 0) {
            goto fail;
        }

        if (qjs_base64url_bignum_set(cx, jwk, "q", q_bn, 0) < 0) {
            goto fail;
        }

        if (qjs_base64url_bignum_set(cx, jwk, "dp", dp_bn, 0) < 0) {
            goto fail;
        }

        if (qjs_base64url_bignum_set(cx, jwk, "dq", dq_bn, 0) < 0) {
            goto fail;
        }

        if (qjs_base64url_bignum_set(cx, jwk, "qi", qi_bn, 0) < 0) {
            goto fail;
        }
    }

    nm = &qjs_webcrypto_alg_name[key->alg->type][key->hash];

    alg = JS_NewStringLen(cx, (char *) nm->start, nm->length);
    if (JS_IsException(alg)) {
        goto fail;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "alg", alg, JS_PROP_C_W_E) < 0) {
        JS_FreeValue(cx, alg);
        goto fail;
    }

    return jwk;

fail:

    JS_FreeValue(cx, jwk);

    return JS_EXCEPTION;
}


static JSValue
qjs_export_jwk_ec(JSContext *cx, qjs_webcrypto_key_t *key)
{
    int             nid, group_bits, group_bytes;
    BIGNUM          *x_bn, *y_bn;
    JSValue         jwk, name;
    njs_str_t       *cname;
    const EC_KEY    *ec;
    const BIGNUM    *d_bn;
    const EC_POINT  *pub;
    const EC_GROUP  *group;

    x_bn = NULL;
    y_bn = NULL;
    d_bn = NULL;
    jwk = JS_UNDEFINED;

    ec = njs_pkey_get_ec_key(key->u.a.pkey);

    pub = EC_KEY_get0_public_key(ec);
    group = EC_KEY_get0_group(ec);

    group_bits = EC_GROUP_get_degree(group);
    group_bytes = (group_bits / 8) + (7 + (group_bits % 8)) / 8;

    x_bn = BN_new();
    if (x_bn == NULL) {
        goto fail;
    }

    y_bn = BN_new();
    if (y_bn == NULL) {
        goto fail;
    }

    if (!njs_ec_point_get_affine_coordinates(group, pub, x_bn, y_bn)) {
        qjs_webcrypto_error(cx, "EC_POINT_get_affine_coordinates() failed");
        goto fail;
    }

    jwk = JS_NewObject(cx);
    if (JS_IsException(jwk)) {
        goto fail;
    }

    if (qjs_base64url_bignum_set(cx, jwk, "x", x_bn, group_bytes) < 0) {
        goto fail;
    }

    BN_free(x_bn);
    x_bn = NULL;

    if (qjs_base64url_bignum_set(cx, jwk, "y", y_bn, group_bytes) < 0) {
        goto fail;
    }

    BN_free(y_bn);
    y_bn = NULL;

    nid = EC_GROUP_get_curve_name(group);

    cname = qjs_algorithm_curve_name(nid);
    if (cname->length == 0) {
        JS_ThrowTypeError(cx, "Unsupported JWK EC curve: %s", OBJ_nid2sn(nid));
        goto fail;
    }

    name = JS_NewStringLen(cx, (char *) cname->start, cname->length);
    if (JS_IsException(name)) {
        goto fail;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "crv", name, JS_PROP_C_W_E) < 0) {
        goto fail;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "kty", JS_NewString(cx, "EC"),
                                  JS_PROP_C_W_E) < 0)
    {
        goto fail;
    }

    if (key->u.a.privat) {
        d_bn = EC_KEY_get0_private_key(ec);

        if (qjs_base64url_bignum_set(cx, jwk, "d", d_bn, group_bytes) < 0) {
            goto fail;
        }
    }

    return jwk;

fail:

    JS_FreeValue(cx, jwk);

    if (x_bn != NULL) {
        BN_free(x_bn);
    }

    if (y_bn != NULL) {
        BN_free(y_bn);
    }

    return JS_EXCEPTION;
}


static JSValue
qjs_export_jwk_asymmetric(JSContext *cx, qjs_webcrypto_key_t *key)
{
    JSValue  jwk, ops;

    njs_assert(key->u.a.pkey != NULL);

    switch (EVP_PKEY_id(key->u.a.pkey)) {
    case EVP_PKEY_RSA:
#if (OPENSSL_VERSION_NUMBER >= 0x10101001L)
    case EVP_PKEY_RSA_PSS:
#endif
        jwk = qjs_export_jwk_rsa(cx, key);
        if (JS_IsException(jwk)) {
            return JS_EXCEPTION;
        }

        break;

    case EVP_PKEY_EC:
        jwk = qjs_export_jwk_ec(cx, key);
        if (JS_IsException(jwk)) {
            return JS_EXCEPTION;
        }

        break;

    default:
        JS_ThrowTypeError(cx, "provided key cannot be exported as JWK");
        return JS_EXCEPTION;
    }

    ops = qjs_key_ops(cx, key->usage);
    if (JS_IsException(ops)) {
        JS_FreeValue(cx, jwk);
        return JS_EXCEPTION;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "key_ops", ops, JS_PROP_C_W_E) < 0) {
        JS_FreeValue(cx, jwk);
        JS_FreeValue(cx, ops);
        return JS_EXCEPTION;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "ext",
                                  JS_NewBool(cx, key->extractable),
                                  JS_PROP_C_W_E) < 0)
    {
        JS_FreeValue(cx, jwk);
        return JS_EXCEPTION;
    }

    return jwk;
}


static JSValue
qjs_export_jwk_oct(JSContext *cx, qjs_webcrypto_key_t *key)
{
    JSValue              val, jwk;
    njs_str_t            *nm;
    qjs_webcrypto_alg_t  type;

    njs_assert(key->u.s.raw.start != NULL);

    jwk = JS_NewObject(cx);
    if (JS_IsException(jwk)) {
        return JS_EXCEPTION;
    }

    val = qjs_string_base64url(cx, &key->u.s.raw);
    if (JS_IsException(val)) {
        goto fail;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "k", val, JS_PROP_C_W_E) < 0) {
        JS_FreeValue(cx, val);
        goto fail;
    }

    type = key->alg->type;

    if (key->alg->type == QJS_ALGORITHM_HMAC) {
        nm = &qjs_webcrypto_alg_name[type][key->hash];
        val = JS_NewStringLen(cx, (char *) nm->start, nm->length);
        if (JS_IsException(val)) {
            goto fail;
        }

    } else {
        switch (key->u.s.raw.length) {
        case 16:
        case 24:
        case 32:
            nm = &qjs_webcrypto_alg_aes_name
                 [type - QJS_ALGORITHM_AES_GCM][(key->u.s.raw.length - 16) / 8];
            val = JS_NewStringLen(cx, (char *) nm->start, nm->length);
            if (JS_IsException(val)) {
                goto fail;
            }

            break;

        default:
            val = JS_UNDEFINED;
            break;
        }
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "alg", val, JS_PROP_C_W_E) < 0) {
        JS_FreeValue(cx, val);
        goto fail;
    }

    val = qjs_key_ops(cx, key->usage);
    if (JS_IsException(val)) {
        goto fail;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "key_ops", val, JS_PROP_C_W_E) < 0) {
        JS_FreeValue(cx, val);
        goto fail;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "kty", JS_NewString(cx, "oct"),
                                  JS_PROP_C_W_E) < 0)
    {
        goto fail;
    }

    if (JS_DefinePropertyValueStr(cx, jwk, "ext",
                                  JS_NewBool(cx, key->extractable),
                                  JS_PROP_C_W_E) < 0)
    {
        goto fail;
    }

    return jwk;

fail:

    JS_FreeValue(cx, jwk);

    return JS_EXCEPTION;

}


static JSValue
qjs_export_raw_ec(JSContext *cx, qjs_webcrypto_key_t *key)
{
    size_t                   size;
    u_char                   *dst;
    const EC_KEY             *ec;
    const EC_GROUP           *group;
    const EC_POINT           *point;
    point_conversion_form_t  form;

    njs_assert(key->u.a.pkey != NULL);

    if (key->u.a.privat) {
        JS_ThrowTypeError(cx, "private key of \"%s\" cannot be exported "
                          "in \"raw\" format", qjs_algorithm_string(key->alg));
        return JS_EXCEPTION;
    }

    ec = njs_pkey_get_ec_key(key->u.a.pkey);

    group = EC_KEY_get0_group(ec);
    point = EC_KEY_get0_public_key(ec);
    form = POINT_CONVERSION_UNCOMPRESSED;

    size = EC_POINT_point2oct(group, point, form, NULL, 0, NULL);
    if (size == 0) {
        qjs_webcrypto_error(cx, "EC_POINT_point2oct() failed");
        return JS_EXCEPTION;
    }

    dst = js_malloc(cx, size);
    if (dst == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    size = EC_POINT_point2oct(group, point, form, dst, size, NULL);
    if (size == 0) {
        js_free(cx, dst);
        qjs_webcrypto_error(cx, "EC_POINT_point2oct() failed");
        return JS_EXCEPTION;
    }

    return qjs_new_array_buffer(cx, dst, size);
}


static JSValue
qjs_webcrypto_derive(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int derive_key)
{
    int                        rc;
    u_char                     *k;
    size_t                     olen;
    int64_t                    iterations, length;
    JSValue                    ret, val, aobject, dobject, dkey_value;
    unsigned                   usage, mask;
    njs_str_t                  salt, info;
    const EVP_MD               *md;
    EVP_PKEY_CTX               *pctx;
    qjs_webcrypto_key_t        *key, *dkey;
    qjs_webcrypto_hash_t       hash;
    qjs_webcrypto_algorithm_t  *alg, *dalg;

    aobject = argv[0];

    alg = qjs_key_algorithm(cx, aobject);
    if (alg == NULL) {
        return JS_EXCEPTION;
    }

    key = JS_GetOpaque2(cx, argv[1], QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    if (key == NULL) {
        JS_ThrowTypeError(cx, "\"baseKey\" is not a CryptoKey object");
        return JS_EXCEPTION;
    }

    mask = derive_key ? QJS_KEY_USAGE_DERIVE_KEY : QJS_KEY_USAGE_DERIVE_BITS;
    if (!(key->usage & mask)) {
        JS_ThrowTypeError(cx, "provide key does not support \"%s\" operation",
                          derive_key ? "deriveKey" : "deriveBits");
        return JS_EXCEPTION;
    }

    if (key->alg != alg) {
        JS_ThrowTypeError(cx, "cannot derive %s using \"%s\" with \"%s\" key",
                          derive_key ? "key" : "bits",
                          qjs_algorithm_string(key->alg),
                          qjs_algorithm_string(alg));
        return JS_EXCEPTION;
    }

    dobject = argv[2];

    if (derive_key) {
        dalg = qjs_key_algorithm(cx, dobject);
        if (dalg == NULL) {
            return JS_EXCEPTION;
        }

        ret = JS_GetPropertyStr(cx, dobject, "length");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (JS_IsUndefined(ret)) {
            JS_ThrowTypeError(cx, "derivedKeyAlgorithm.length is not provided");
            return JS_EXCEPTION;
        }

    } else {
        dalg = NULL;
        ret = JS_DupValue(cx, dobject);
    }

    if (JS_ToInt64(cx, &length, ret) < 0) {
        return JS_EXCEPTION;
    }

    JS_FreeValue(cx, ret);

    dkey = NULL;
    length /= 8;
    dkey_value = JS_UNDEFINED;

    if (derive_key) {
        switch (dalg->type) {
        case QJS_ALGORITHM_AES_GCM:
        case QJS_ALGORITHM_AES_CTR:
        case QJS_ALGORITHM_AES_CBC:

            if (length != 16 && length != 32) {
                JS_ThrowTypeError(cx, "deriveKey \"%s\" length must be "
                                  "128 or 256", qjs_algorithm_string(dalg));
                return JS_EXCEPTION;
            }

            break;

        default:
            JS_ThrowTypeError(cx, "not implemented deriveKey: \"%s\"",
                              qjs_algorithm_string(dalg));
            return JS_EXCEPTION;
        }

        ret = qjs_key_usage(cx, argv[4], &usage);
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (usage & ~dalg->usage) {
            JS_ThrowTypeError(cx, "unsupported key usage for \"%s\" key",
                              qjs_algorithm_string(alg));
            return JS_EXCEPTION;
        }

        dkey_value = qjs_webcrypto_key_make(cx, dalg, usage, 0);
        if (JS_IsException(dkey_value)) {
            JS_ThrowOutOfMemory(cx);
            return JS_EXCEPTION;
        }

        dkey = JS_GetOpaque(dkey_value, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    }

    k = js_malloc(cx, length);
    if (k == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    switch (alg->type) {
    case QJS_ALGORITHM_PBKDF2:
        ret = qjs_algorithm_hash(cx, aobject, &hash);
        if (JS_IsException(ret)) {
            goto fail;
        }

        val = JS_GetPropertyStr(cx, aobject, "salt");
        if (JS_IsException(val)) {
            goto fail;
        }

        ret = qjs_typed_array_data(cx, val, &salt);
        JS_FreeValue(cx, val);
        if (JS_IsException(ret)) {
            goto fail;
        }

        if (salt.length < 16) {
            JS_ThrowTypeError(cx, "PBKDF2 algorithm.salt must be at least "
                              "16 bytes long");
            goto fail;
        }

        ret = JS_GetPropertyStr(cx, aobject, "iterations");
        if (JS_IsException(ret)) {
            goto fail;
        }

        if (JS_IsUndefined(ret)) {
            JS_ThrowTypeError(cx, "PBKDF2 algorithm.iterations is not provided");
            goto fail;
        }

        if (JS_ToInt64(cx, &iterations, ret) < 0) {
            goto fail;
        }

        JS_FreeValue(cx, ret);

        md = qjs_algorithm_hash_digest(hash);

        rc = PKCS5_PBKDF2_HMAC((char *) key->u.s.raw.start, key->u.s.raw.length,
                               salt.start, salt.length, iterations, md, length,
                               k);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "PKCS5_PBKDF2_HMAC() failed");
            goto fail;
        }

        break;

    case QJS_ALGORITHM_HKDF:
#ifdef EVP_PKEY_HKDF
        ret = qjs_algorithm_hash(cx, aobject, &hash);
        if (JS_IsException(ret)) {
            goto fail;
        }

        val = JS_GetPropertyStr(cx, aobject, "salt");
        if (JS_IsException(val)) {
            goto fail;
        }

        ret = qjs_typed_array_data(cx, val, &salt);
        JS_FreeValue(cx, val);
        if (JS_IsException(ret)) {
            goto fail;
        }

        val = JS_GetPropertyStr(cx, aobject, "info");
        if (JS_IsException(val)) {
            goto fail;
        }

        ret = qjs_typed_array_data(cx, val, &info);
        JS_FreeValue(cx, val);
        if (JS_IsException(ret)) {
            goto fail;
        }

        pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
        if (pctx == NULL) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_new_id() failed");
            goto fail;
        }

        rc = EVP_PKEY_derive_init(pctx);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_derive_init() failed");
            goto free;
        }

        md = qjs_algorithm_hash_digest(hash);

        rc = EVP_PKEY_CTX_set_hkdf_md(pctx, md);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_set_hkdf_md() failed");
            goto free;
        }

        rc = EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt.start, salt.length);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_set1_hkdf_salt() failed");
            goto free;
        }

        rc = EVP_PKEY_CTX_set1_hkdf_key(pctx, key->u.s.raw.start,
                                        key->u.s.raw.length);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_set1_hkdf_key() failed");
            goto free;
        }

        rc = EVP_PKEY_CTX_add1_hkdf_info(pctx, info.start, info.length);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_add1_hkdf_info() failed");
            goto free;
        }

        olen = (size_t) length;
        rc = EVP_PKEY_derive(pctx, k, &olen);
        if (rc <= 0 || olen != (size_t) length) {
            qjs_webcrypto_error(cx, "EVP_PKEY_derive() failed");
            goto free;
        }

free:
        EVP_PKEY_CTX_free(pctx);

        if (rc <= 0) {
            goto fail;
        }

        break;
#else
        (void) pctx;
        (void) olen;
        (void) &string_info;
        (void) &info;
#endif

    case QJS_ALGORITHM_ECDH:
    default:
        JS_ThrowTypeError(cx, "not implemented deriveKey algorithm: \"%s\"",
                          qjs_algorithm_string(alg));
        goto fail;
    }

    if (derive_key) {
        if (dalg->type == QJS_ALGORITHM_HMAC) {
            ret = qjs_algorithm_hash(cx, dobject, &dkey->hash);
            if (JS_IsException(ret)) {
                goto fail;
            }
        }

        dkey->u.s.raw.start = k;
        dkey->u.s.raw.length = length;

        ret = dkey_value;

    } else {

        ret = qjs_new_array_buffer(cx, k, length);
    }

    return qjs_webcrypto_result(cx, ret, 0);

fail:

    JS_FreeValue(cx, dkey_value);

    js_free(cx, k);

    return qjs_webcrypto_result(cx, JS_UNDEFINED, -1);
}


static JSValue
qjs_webcrypto_digest(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue               ret;
    u_char                *dst;
    unsigned              olen;
    njs_str_t             data;
    const EVP_MD          *md;
    qjs_webcrypto_hash_t  hash;

    ret = qjs_algorithm_hash(cx, argv[0], &hash);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;
    }

    ret = qjs_typed_array_data(cx, argv[1], &data);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;
    }

    md = qjs_algorithm_hash_digest(hash);
    olen = EVP_MD_size(md);

    dst = js_malloc(cx, olen);
    if (dst == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    if (EVP_Digest(data.start, data.length, dst, &olen, md, NULL) <= 0) {
        js_free(cx, dst);
        qjs_webcrypto_error(cx, "EVP_Digest() failed");
        return JS_EXCEPTION;
    }

    ret = qjs_new_array_buffer(cx, dst, olen);

    return ret;
}


static JSValue
qjs_webcrypto_export_key(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    BIO                         *bio;
    BUF_MEM                     *mem;
    JSValue                     ret;
    qjs_webcrypto_key_t         *key;
    PKCS8_PRIV_KEY_INFO         *pkcs8;
    qjs_webcrypto_key_format_t  fmt;

    fmt = qjs_key_format(cx, argv[0]);

    key = JS_GetOpaque2(cx, argv[1], QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    if (key == NULL) {
        JS_ThrowTypeError(cx, "\"key\" is not a CryptoKey object");
        return JS_EXCEPTION;
    }

    if (!(fmt & key->alg->fmt)) {
        JS_ThrowTypeError(cx, "unsupported key fmt \"%s\" for \"%s\" key",
                          qjs_format_string(fmt),
                          qjs_algorithm_string(key->alg));
        return JS_EXCEPTION;
    }

    if (!key->extractable) {
        JS_ThrowTypeError(cx, "provided key cannot be extracted");
        return JS_EXCEPTION;
    }

    switch (fmt) {
    case QJS_KEY_FORMAT_JWK:
        switch (key->alg->type) {
        case QJS_ALGORITHM_RSASSA_PKCS1_v1_5:
        case QJS_ALGORITHM_RSA_PSS:
        case QJS_ALGORITHM_RSA_OAEP:
        case QJS_ALGORITHM_ECDSA:
            ret = qjs_export_jwk_asymmetric(cx, key);
            if (JS_IsException(ret)) {
                goto fail;
            }

            break;

        case QJS_ALGORITHM_AES_GCM:
        case QJS_ALGORITHM_AES_CTR:
        case QJS_ALGORITHM_AES_CBC:
        case QJS_ALGORITHM_HMAC:
            ret = qjs_export_jwk_oct(cx, key);
            if (JS_IsException(ret)) {
                goto fail;
            }

            break;

        default:
            JS_ThrowTypeError(cx, "provided key of \"%s\" cannot be exported "
                              "as JWK", qjs_algorithm_string(key->alg));
            goto fail;
        }

        break;

    case QJS_KEY_FORMAT_PKCS8:
        if (!key->u.a.privat) {
            JS_ThrowTypeError(cx, "public key of \"%s\" cannot be exported "
                              "as PKCS8", qjs_algorithm_string(key->alg));
            goto fail;
        }

        bio = BIO_new(BIO_s_mem());
        if (bio == NULL) {
            qjs_webcrypto_error(cx, "BIO_new(BIO_s_mem()) failed");
            goto fail;
        }

        njs_assert(key->u.a.pkey != NULL);

        pkcs8 = EVP_PKEY2PKCS8(key->u.a.pkey);
        if (pkcs8 == NULL) {
            BIO_free(bio);
            qjs_webcrypto_error(cx, "EVP_PKEY2PKCS8() failed");
            goto fail;
        }

        if (!i2d_PKCS8_PRIV_KEY_INFO_bio(bio, pkcs8)) {
            BIO_free(bio);
            PKCS8_PRIV_KEY_INFO_free(pkcs8);
            qjs_webcrypto_error(cx, "i2d_PKCS8_PRIV_KEY_INFO_bio() failed");
            goto fail;
        }

        BIO_get_mem_ptr(bio, &mem);

        ret = JS_NewArrayBufferCopy(cx, (const uint8_t *) mem->data,
                                    mem->length);

        BIO_free(bio);
        PKCS8_PRIV_KEY_INFO_free(pkcs8);

        if (JS_IsException(ret)) {
            goto fail;
        }

        break;

    case QJS_KEY_FORMAT_SPKI:
        if (key->u.a.privat) {
            JS_ThrowTypeError(cx, "private key of \"%s\" cannot be exported "
                              "as SPKI", qjs_algorithm_string(key->alg));
            goto fail;
        }

        bio = BIO_new(BIO_s_mem());
        if (bio == NULL) {
            qjs_webcrypto_error(cx, "BIO_new(BIO_s_mem()) failed");
            goto fail;
        }

        njs_assert(key->u.a.pkey != NULL);

        if (!i2d_PUBKEY_bio(bio, key->u.a.pkey)) {
            BIO_free(bio);
            qjs_webcrypto_error(cx, "i2d_PUBKEY_bio() failed");
            goto fail;
        }

        BIO_get_mem_ptr(bio, &mem);

        ret = JS_NewArrayBufferCopy(cx, (const uint8_t *) mem->data,
                                    mem->length);

        BIO_free(bio);

        if (JS_IsException(ret)) {
            goto fail;
        }

        break;

    case QJS_KEY_FORMAT_RAW:
    default:
        if (key->alg->type == QJS_ALGORITHM_ECDSA) {
            ret = qjs_export_raw_ec(cx, key);
            if (JS_IsException(ret)) {
                goto fail;
            }

            break;
        } else {
            ret = JS_NewArrayBufferCopy(cx, key->u.s.raw.start,
                                        key->u.s.raw.length);
            if (JS_IsException(ret)) {
                goto fail;
            }
        }

    }

    return qjs_webcrypto_result(cx, ret, 0);

fail:

    return qjs_webcrypto_result(cx, JS_UNDEFINED, -1);
}


static JSValue
qjs_webcrypto_generate_key(JSContext *cx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    int                        n, extractable;
    JSValue                    ret, key, keypub, options, obj;
    unsigned                   usage;
    EVP_PKEY_CTX               *ctx;
    qjs_webcrypto_key_t        *wkey, *wkeypub;
    qjs_webcrypto_algorithm_t  *alg;

    ctx = NULL;
    options = argv[0];
    key = JS_UNDEFINED;
    keypub = JS_UNDEFINED;

    alg = qjs_key_algorithm(cx, options);
    if (alg == NULL) {
        return JS_EXCEPTION;
    }

    ret = qjs_key_usage(cx, argv[2], &usage);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;
    }

    if (usage & ~alg->usage) {
        JS_ThrowTypeError(cx, "unsupported key usage for \"%s\" key",
                          qjs_algorithm_string(alg));
        return JS_EXCEPTION;
    }

    extractable = JS_ToBool(cx, argv[1]);
    key = qjs_webcrypto_key_make(cx, alg, usage, extractable);
    if (JS_IsException(key)) {
        return JS_EXCEPTION;
    }

    wkey = JS_GetOpaque(key, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);

    switch (alg->type) {
    case QJS_ALGORITHM_RSASSA_PKCS1_v1_5:
    case QJS_ALGORITHM_RSA_PSS:
    case QJS_ALGORITHM_RSA_OAEP:
        ret = qjs_algorithm_hash(cx, options, &wkey->hash);
        if (JS_IsException(ret)) {
            goto fail;
        }

        ret = JS_GetPropertyStr(cx, options, "modulusLength");
        if (JS_IsException(ret)) {
            goto fail;
        }

        if (!JS_IsNumber(ret)) {
            JS_FreeValue(cx, ret);
            JS_ThrowTypeError(cx, "\"modulusLength\" is not a number");
            goto fail;
        }

        if (JS_ToInt32(cx, &n, ret) < 0) {
            goto fail;
        }

        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
        if (ctx == NULL) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_new_id() failed");
            goto fail;
        }

        if (EVP_PKEY_keygen_init(ctx) <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_keygen_init() failed");
            goto fail;
        }

        if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, n) <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_set_rsa_keygen_bits() "
                                "failed");
            goto fail;
        }

        if (EVP_PKEY_keygen(ctx, &wkey->u.a.pkey) <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_keygen() failed");
            goto fail;
        }

        EVP_PKEY_CTX_free(ctx);
        ctx = NULL;

        wkey->u.a.privat = 1;
        wkey->usage = (alg->type == QJS_ALGORITHM_RSA_OAEP)
                        ? QJS_KEY_USAGE_DECRYPT
                        : QJS_KEY_USAGE_SIGN;

        keypub = qjs_webcrypto_key_make(cx, alg, usage, extractable);
        if (JS_IsException(keypub)) {
            goto fail;
        }

        if (njs_pkey_up_ref(wkey->u.a.pkey) <= 0) {
            qjs_webcrypto_error(cx, "qjs_pkey_up_ref() failed");
            goto fail;
        }

        wkeypub = JS_GetOpaque(keypub, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);

        wkeypub->u.a.pkey = wkey->u.a.pkey;
        wkeypub->hash = wkey->hash;
        wkeypub->usage = (alg->type == QJS_ALGORITHM_RSA_OAEP)
                          ? QJS_KEY_USAGE_ENCRYPT
                          : QJS_KEY_USAGE_VERIFY;

        obj = JS_NewObject(cx);
        if (JS_IsException(obj)) {
            goto fail;
        }

        if (JS_SetPropertyStr(cx, obj, "privateKey", key) < 0) {
            goto fail;
        }

        key = JS_UNDEFINED;

        if (JS_SetPropertyStr(cx, obj, "publicKey", keypub) < 0) {
            goto fail;
        }

        break;

    case QJS_ALGORITHM_ECDSA:
        ret = qjs_algorithm_curve(cx, options, &wkey->u.a.curve);
        if (JS_IsException(ret)) {
            goto fail;
        }

        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
        if (ctx == NULL) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_new_id() failed");
            goto fail;
        }

        if (EVP_PKEY_keygen_init(ctx) <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_keygen_init() failed");
            goto fail;
        }

        if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, wkey->u.a.curve) <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_set_ec_paramgen_curve_nid() "
                                "failed");
            goto fail;
        }

        if (EVP_PKEY_keygen(ctx, &wkey->u.a.pkey) <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_keygen() failed");
            goto fail;
        }

        EVP_PKEY_CTX_free(ctx);
        ctx = NULL;

        wkey->u.a.privat = 1;
        wkey->usage = QJS_KEY_USAGE_SIGN;

        keypub = qjs_webcrypto_key_make(cx, alg, usage, extractable);
        if (JS_IsException(keypub)) {
            goto fail;
        }

        if (njs_pkey_up_ref(wkey->u.a.pkey) <= 0) {
            qjs_webcrypto_error(cx, "qjs_pkey_up_ref() failed");
            goto fail;
        }

        wkeypub = JS_GetOpaque(keypub, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);

        wkeypub->u.a.pkey = wkey->u.a.pkey;
        wkeypub->u.a.curve = wkey->u.a.curve;
        wkeypub->usage = QJS_KEY_USAGE_VERIFY;

        obj = JS_NewObject(cx);
        if (JS_IsException(obj)) {
            goto fail;
        }

        if (JS_SetPropertyStr(cx, obj, "privateKey", key) < 0) {
            goto fail;
        }

        key = JS_UNDEFINED;

        if (JS_SetPropertyStr(cx, obj, "publicKey", keypub) < 0) {
            goto fail;
        }

        break;

    case QJS_ALGORITHM_AES_GCM:
    case QJS_ALGORITHM_AES_CTR:
    case QJS_ALGORITHM_AES_CBC:
    case QJS_ALGORITHM_HMAC:
        if (alg->type == QJS_ALGORITHM_HMAC) {
            ret = qjs_algorithm_hash(cx, options, &wkey->hash);
            if (JS_IsException(ret)) {
                goto fail;
            }

            wkey->u.s.raw.length =
                            EVP_MD_size(qjs_algorithm_hash_digest(wkey->hash));

        } else {
            ret = JS_GetPropertyStr(cx, options, "length");
            if (JS_IsException(ret)) {
                goto fail;
            }

            if (!JS_IsNumber(ret)) {
                JS_FreeValue(cx, ret);
                JS_ThrowTypeError(cx, "length is not a number");
                goto fail;
            }

            if (JS_ToInt32(cx, &n, ret) < 0) {
                goto fail;
            }

            wkey->u.s.raw.length = n / 8;

            if (wkey->u.s.raw.length != 16
                && wkey->u.s.raw.length != 24
                && wkey->u.s.raw.length != 32)
            {
                JS_ThrowTypeError(cx, "length for \"%s\" key should be "
                                  "one of 128, 192, 256",
                                  qjs_algorithm_string(alg));
                goto fail;
            }
        }

        wkey->u.s.raw.start = js_malloc(cx, wkey->u.s.raw.length);
        if (wkey->u.s.raw.start == NULL) {
            JS_ThrowOutOfMemory(cx);
            goto fail;
        }

        if (RAND_bytes(wkey->u.s.raw.start, wkey->u.s.raw.length) <= 0) {
            qjs_webcrypto_error(cx, "RAND_bytes() failed");
            goto fail;
        }

        obj = key;

        break;

    default:
        JS_ThrowTypeError(cx, "not implemented generateKey algorithm: \"%s\"",
                          qjs_algorithm_string(alg));
        goto fail;
    }

    return qjs_webcrypto_result(cx, obj, 0);

fail:

    if (ctx != NULL) {
        EVP_PKEY_CTX_free(ctx);
    }

    JS_FreeValue(cx, key);
    JS_FreeValue(cx, keypub);

    return qjs_webcrypto_result(cx, JS_UNDEFINED, -1);
}


static BIGNUM *
qjs_import_base64url_bignum(JSContext *cx, JSValue value)
{
    BIGNUM     *bn;
    njs_str_t  data, decoded;
    u_char     buf[512];

    data.start = (u_char *) JS_ToCStringLen(cx, &data.length, value);
    if (data.start == NULL) {
        JS_ThrowOutOfMemory(cx);
        return NULL;
    }

    decoded.length = qjs_base64url_decode_length(cx, &data);

    if (decoded.length > sizeof(buf)) {
        JS_ThrowRangeError(cx, "JWK key too long: %zu > 512", decoded.length);
        return NULL;
    }

    decoded.start = buf;

    qjs_base64url_decode(cx, &data, &decoded);

    bn = BN_bin2bn(decoded.start, decoded.length, NULL);
    JS_FreeCString(cx, (char *) data.start);

    return bn;
}


static EVP_PKEY *
qjs_import_jwk_rsa(JSContext *cx, JSValue jwk, qjs_webcrypto_key_t *key)
{
    RSA                    *rsa;
    BIGNUM                 *n_bn, *e_bn, *d_bn, *p_bn, *q_bn, *dp_bn, *dq_bn,
                           *qi_bn;
    JSValue                ret, val, n, e, d, p, q, dp, dq, qi;
    unsigned               usage;
    EVP_PKEY               *pkey;
    njs_str_t              alg;
    qjs_webcrypto_entry_t  *w;

    e = JS_UNDEFINED;
    d = JS_UNDEFINED;

    n = JS_GetPropertyStr(cx, jwk, "n");
    if (JS_IsException(n)) {
        goto fail0;
    }

    e = JS_GetPropertyStr(cx, jwk, "e");
    if (JS_IsException(e)) {
        goto fail0;
    }

    d = JS_GetPropertyStr(cx, jwk, "d");
    if (JS_IsException(d)) {
        goto fail0;
    }

    if (!JS_IsString(n)
        || !JS_IsString(e)
        || (!JS_IsUndefined(d) && !JS_IsString(d)))
    {
fail0:
        JS_FreeValue(cx, n);
        JS_FreeValue(cx, e);
        JS_FreeValue(cx, d);
        JS_ThrowTypeError(cx, "Invalid JWK RSA key");
        return NULL;
    }

    key->u.a.privat = JS_IsString(d);

    val = JS_GetPropertyStr(cx, jwk, "key_ops");
    if (!JS_IsException(val) && !JS_IsUndefined(val)) {
        ret = qjs_key_usage(cx, val, &usage);
        JS_FreeValue(cx, val);
        if (JS_IsException(ret)) {
            goto fail0;
        }

        if ((key->usage & usage) != key->usage) {
            JS_ThrowTypeError(cx, "Key operations and usage mismatch");
            goto fail0;
        }
    }

    ret = JS_GetPropertyStr(cx, jwk, "alg");
    if (!JS_IsException(ret) && !JS_IsUndefined(ret)) {
        alg.start = (u_char *) JS_ToCStringLen(cx, &alg.length, ret);
        JS_FreeValue(cx, ret);
        if (alg.start == NULL) {
            JS_ThrowOutOfMemory(cx);
            goto fail0;
        }

        for (w = &qjs_webcrypto_alg_hash[0]; w->name.length != 0; w++) {
            if (njs_strstr_eq(&alg, &w->name)) {
                key->hash = w->value;
                break;
            }
        }

        JS_FreeCString(cx, (char *) alg.start);
    }

    if (key->extractable) {
        ret = JS_GetPropertyStr(cx, jwk, "ext");
        if (!JS_IsException(ret)
            && !JS_IsUndefined(ret)
            && !JS_ToBool(cx, ret))
        {
            JS_FreeValue(cx, ret);
            JS_ThrowTypeError(cx, "JWK RSA is not extractable");
            goto fail0;
        }

        JS_FreeValue(cx, ret);
    }

    rsa = RSA_new();
    if (rsa == NULL) {
        qjs_webcrypto_error(cx, "RSA_new() failed");
        goto fail0;
    }

    p = JS_UNDEFINED;
    q = JS_UNDEFINED;
    dp = JS_UNDEFINED;
    dq = JS_UNDEFINED;
    qi = JS_UNDEFINED;

    n_bn = qjs_import_base64url_bignum(cx, n);
    if (n_bn == NULL) {
        goto fail;
    }

    e_bn = qjs_import_base64url_bignum(cx, e);
    if (e_bn == NULL) {
        BN_free(n_bn);
        goto fail;
    }

    if (!njs_rsa_set0_key(rsa, n_bn, e_bn, NULL)) {
        BN_free(n_bn);
        BN_free(e_bn);
        qjs_webcrypto_error(cx, "RSA_set0_key() failed");
        goto fail;
    }

    if (!key->u.a.privat) {
        goto done;
    }

    p = JS_GetPropertyStr(cx, jwk, "p");
    if (JS_IsException(p)) {
        goto fail1;
    }

    q = JS_GetPropertyStr(cx, jwk, "q");
    if (JS_IsException(q)) {
        goto fail1;
    }

    dp = JS_GetPropertyStr(cx, jwk, "dp");
    if (JS_IsException(dp)) {
        goto fail1;
    }

    dq = JS_GetPropertyStr(cx, jwk, "dq");
    if (JS_IsException(dq)) {
        goto fail1;
    }

    qi = JS_GetPropertyStr(cx, jwk, "qi");
    if (JS_IsException(qi)) {
        goto fail1;
    }

    if (!JS_IsString(d)
        || !JS_IsString(p)
        || !JS_IsString(q)
        || !JS_IsString(dp)
        || !JS_IsString(dq)
        || !JS_IsString(qi))
    {
fail1:
        JS_ThrowTypeError(cx, "Invalid JWK RSA key");
        goto fail;
    }

    d_bn = qjs_import_base64url_bignum(cx, d);
    if (d_bn == NULL) {
        goto fail;
    }

    if (!njs_rsa_set0_key(rsa, NULL, NULL, d_bn)) {
        BN_free(d_bn);
        qjs_webcrypto_error(cx, "RSA_set0_key() failed");
        goto fail;
    }

    p_bn = qjs_import_base64url_bignum(cx, p);
    if (p_bn == NULL) {
        goto fail;
    }

    q_bn = qjs_import_base64url_bignum(cx, q);
    if (q_bn == NULL) {
        BN_free(p_bn);
        goto fail;
    }

    if (!njs_rsa_set0_factors(rsa, p_bn, q_bn)) {
        BN_free(p_bn);
        BN_free(q_bn);

        qjs_webcrypto_error(cx, "RSA_set0_factors() failed");
        goto fail;
    }

    dp_bn = qjs_import_base64url_bignum(cx, dp);
    if (dp_bn == NULL) {
        goto fail;
    }

    dq_bn = qjs_import_base64url_bignum(cx, dq);
    if (dq_bn == NULL) {
        BN_free(dp_bn);
        goto fail;
    }

    qi_bn = qjs_import_base64url_bignum(cx, qi);
    if (qi_bn == NULL) {
        BN_free(dp_bn);
        BN_free(dq_bn);
        goto fail;
    }

    if (!njs_rsa_set0_ctr_params(rsa, dp_bn, dq_bn, qi_bn)) {
        BN_free(dp_bn);
        BN_free(dq_bn);
        BN_free(qi_bn);
        qjs_webcrypto_error(cx, "RSA_set0_crt_params() failed");
        goto fail;
    }

    JS_FreeValue(cx, p);
    JS_FreeValue(cx, q);
    JS_FreeValue(cx, dp);
    JS_FreeValue(cx, dq);
    JS_FreeValue(cx, qi);

done:

    JS_FreeValue(cx, n);
    JS_FreeValue(cx, e);
    JS_FreeValue(cx, d);

    pkey = EVP_PKEY_new();
    if (pkey == NULL) {
        goto fail;
    }

    if (!EVP_PKEY_set1_RSA(pkey, rsa)) {
        EVP_PKEY_free(pkey);
        goto fail;
    }

    RSA_free(rsa);

    return pkey;

fail:

    JS_FreeValue(cx, n);
    JS_FreeValue(cx, e);
    JS_FreeValue(cx, d);
    JS_FreeValue(cx, p);
    JS_FreeValue(cx, q);
    JS_FreeValue(cx, dp);
    JS_FreeValue(cx, dq);
    JS_FreeValue(cx, qi);

    RSA_free(rsa);

    return NULL;
}


static EVP_PKEY *
qjs_import_jwk_ec(JSContext *cx, JSValue jwk, qjs_webcrypto_key_t *key)
{
    int                    curve;
    EC_KEY                 *ec;
    BIGNUM                 *x_bn, *y_bn, *d_bn;
    JSValue                ret, val, x, y, d;
    unsigned               usage;
    EVP_PKEY               *pkey;
    njs_str_t              name;
    qjs_webcrypto_entry_t  *e;

    x = JS_UNDEFINED;
    y = JS_UNDEFINED;
    d = JS_UNDEFINED;

    x = JS_GetPropertyStr(cx, jwk, "x");
    if (JS_IsException(x)) {
        goto fail0;
    }

    y = JS_GetPropertyStr(cx, jwk, "y");
    if (JS_IsException(y)) {
        goto fail0;
    }

    d = JS_GetPropertyStr(cx, jwk, "d");
    if (JS_IsException(d)) {
        goto fail0;
    }

    if (!JS_IsString(x)
        || !JS_IsString(y)
        || (!JS_IsUndefined(d) && !JS_IsString(d)))
    {
fail0:
        JS_FreeValue(cx, x);
        JS_FreeValue(cx, y);
        JS_FreeValue(cx, d);
        JS_ThrowTypeError(cx, "Invalid JWK EC key");
        return NULL;
    }

    key->u.a.privat = JS_IsString(d);

    val = JS_GetPropertyStr(cx, jwk, "key_ops");
    if (!JS_IsException(val) && !JS_IsUndefined(val)) {
        ret = qjs_key_usage(cx, val, &usage);
        JS_FreeValue(cx, val);
        if (JS_IsException(ret)) {
            goto fail0;
        }

        if ((key->usage & usage) != key->usage) {
            JS_ThrowTypeError(cx, "Key operations and usage mismatch");
            goto fail0;
        }
    }

    if (key->extractable) {
        ret = JS_GetPropertyStr(cx, jwk, "ext");
        if (!JS_IsException(ret)
            && !JS_IsUndefined(ret)
            && !JS_ToBool(cx, ret))
        {
            JS_FreeValue(cx, ret);
            JS_ThrowTypeError(cx, "JWK EC is not extractable");
            goto fail0;
        }

        JS_FreeValue(cx, ret);
    }

    curve = 0;

    ret = JS_GetPropertyStr(cx, jwk, "crv");
    if (!JS_IsException(ret) && !JS_IsUndefined(ret)) {
        name.start = (u_char *) JS_ToCStringLen(cx, &name.length, ret);
        JS_FreeValue(cx, ret);
        if (name.start == NULL) {
            JS_ThrowOutOfMemory(cx);
            goto fail0;
        }

        for (e = &qjs_webcrypto_curve[0]; e->name.length != 0; e++) {
            if (njs_strstr_eq(&name, &e->name)) {
                curve = e->value;
                break;
            }
        }

        JS_FreeCString(cx, (char *) name.start);
    }

    if (curve != key->u.a.curve) {
        JS_ThrowTypeError(cx, "JWK EC curve mismatch");
        goto fail0;
    }

    ec = EC_KEY_new_by_curve_name(key->u.a.curve);
    if (ec == NULL) {
        qjs_webcrypto_error(cx, "EC_KEY_new_by_curve_name() failed");
        goto fail0;
    }

    y_bn = NULL;
    d_bn = NULL;

    x_bn = qjs_import_base64url_bignum(cx, x);
    if (x_bn == NULL) {
        goto fail;
    }

    y_bn = qjs_import_base64url_bignum(cx, y);
    if (y_bn == NULL) {
        goto fail;
    }

    if (!EC_KEY_set_public_key_affine_coordinates(ec, x_bn, y_bn)) {
        qjs_webcrypto_error(cx, "EC_KEY_set_public_key_affine_coordinates() "
                            "failed");
        goto fail;
    }

    BN_free(x_bn);
    x_bn = NULL;

    BN_free(y_bn);
    y_bn = NULL;

    if (key->u.a.privat) {
        d_bn = qjs_import_base64url_bignum(cx, d);
        if (d_bn == NULL) {
            goto fail;
        }

        if (!EC_KEY_set_private_key(ec, d_bn)) {
            qjs_webcrypto_error(cx, "EC_KEY_set_private_key() failed");
            goto fail;
        }

        BN_free(d_bn);
        d_bn = NULL;
    }

    pkey = EVP_PKEY_new();
    if (pkey == NULL) {
        goto fail;
    }

    if (!EVP_PKEY_set1_EC_KEY(pkey, ec)) {
        qjs_webcrypto_error(cx, "EVP_PKEY_set1_EC_KEY() failed");
        goto fail_pkey;
    }

    EC_KEY_free(ec);

    JS_FreeValue(cx, x);
    JS_FreeValue(cx, y);
    JS_FreeValue(cx, d);

    return pkey;

fail_pkey:

    EVP_PKEY_free(pkey);

fail:

    EC_KEY_free(ec);

    if (x_bn != NULL) {
        BN_free(x_bn);
    }

    if (y_bn != NULL) {
        BN_free(y_bn);
    }

    if (d_bn != NULL) {
        BN_free(d_bn);
    }

    JS_FreeValue(cx, x);
    JS_FreeValue(cx, y);
    JS_FreeValue(cx, d);

    return NULL;
}


static EVP_PKEY *
qjs_import_raw_ec(JSContext *cx, njs_str_t *data, qjs_webcrypto_key_t *key)
{
    EC_KEY          *ec;
    EVP_PKEY        *pkey;
    EC_POINT        *pub;
    const EC_GROUP  *group;

    ec = EC_KEY_new_by_curve_name(key->u.a.curve);
    if (ec == NULL) {
        qjs_webcrypto_error(cx, "EC_KEY_new_by_curve_name() failed");
        return NULL;
    }

    group = EC_KEY_get0_group(ec);

    pub = EC_POINT_new(group);
    if (pub == NULL) {
        EC_KEY_free(ec);
        qjs_webcrypto_error(cx, "EC_POINT_new() failed");
        return NULL;
    }

    if (!EC_POINT_oct2point(group, pub, data->start, data->length, NULL)) {
        EC_KEY_free(ec);
        EC_POINT_free(pub);
        qjs_webcrypto_error(cx, "EC_POINT_oct2point() failed");
        return NULL;
    }

    if (!EC_KEY_set_public_key(ec, pub)) {
        EC_KEY_free(ec);
        EC_POINT_free(pub);
        qjs_webcrypto_error(cx, "EC_KEY_set_public_key() failed");
        return NULL;
    }

    pkey = EVP_PKEY_new();
    if (pkey == NULL) {
        EC_KEY_free(ec);
        EC_POINT_free(pub);
        qjs_webcrypto_error(cx, "EVP_PKEY_new() failed");
        return NULL;
    }

    if (!EVP_PKEY_set1_EC_KEY(pkey, ec)) {
        EC_KEY_free(ec);
        EC_POINT_free(pub);
        EVP_PKEY_free(pkey);
        qjs_webcrypto_error(cx, "EVP_PKEY_set1_EC_KEY() failed");
        return NULL;
    }

    EC_KEY_free(ec);
    EC_POINT_free(pub);

    return pkey;
}


static JSValue
qjs_import_jwk_oct(JSContext *cx, JSValue jwk, qjs_webcrypto_key_t *key)
{
    size_t                 size;
    unsigned               usage;
    JSValue                val, ret;
    njs_str_t              *a, alg, b64;
    qjs_webcrypto_alg_t    type;
    qjs_webcrypto_entry_t  *w;

    static qjs_webcrypto_entry_t hashes[] = {
        { njs_str("HS1"), QJS_HASH_SHA1 },
        { njs_str("HS256"), QJS_HASH_SHA256 },
        { njs_str("HS384"), QJS_HASH_SHA384 },
        { njs_str("HS512"), QJS_HASH_SHA512 },
        { njs_null_str, 0 }
    };

    val = JS_GetPropertyStr(cx, jwk, "k");
    if (JS_IsException(val) || !JS_IsString(val)) {
        JS_ThrowTypeError(cx, "Invalid JWK oct key");
        return JS_EXCEPTION;
    }

    b64.start = (u_char *) JS_ToCStringLen(cx, &b64.length, val);
    JS_FreeValue(cx, val);
    if (b64.start == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    size = qjs_base64url_decode_length(cx, &b64);

    key->u.s.raw.length = size;
    key->u.s.raw.start = js_malloc(cx, size);
    if (key->u.s.raw.start == NULL) {
        JS_FreeCString(cx, (char *) b64.start);
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    qjs_base64url_decode(cx, &b64, &key->u.s.raw);
    JS_FreeCString(cx, (char *) b64.start);

    val = JS_GetPropertyStr(cx, jwk, "alg");
    if (JS_IsException(val)) {
        return JS_EXCEPTION;
    }

    if (!JS_IsString(val)) {
        JS_FreeValue(cx, val);
        return JS_ThrowTypeError(cx, "Invalid JWK oct alg");
    }

    alg.start = (u_char *) JS_ToCStringLen(cx, &alg.length, val);
    JS_FreeValue(cx, val);
    if (alg.start == NULL) {
        JS_ThrowOutOfMemory(cx);
        return JS_EXCEPTION;
    }

    size = 16;

    if (key->alg->type == QJS_ALGORITHM_HMAC) {
        for (w = &hashes[0]; w->name.length != 0; w++) {
            if (njs_strstr_eq(&alg, &w->name)) {
                key->hash = w->value;
                goto done;
            }
        }

    } else {
        type = key->alg->type;
        a = &qjs_webcrypto_alg_aes_name[type - QJS_ALGORITHM_AES_GCM][0];
        for (; a->length != 0; a++) {
            if (njs_strstr_eq(&alg, a)) {
                goto done;
            }

            size += 8;
        }
    }

    JS_ThrowTypeError(cx, "unexpected \"alg\" value \"%s\" for JWK key",
                      alg.start);
    JS_FreeCString(cx, (char *) alg.start);
    return JS_EXCEPTION;

done:

    if (key->alg->type != QJS_ALGORITHM_HMAC) {
        if (key->u.s.raw.length != size) {
            JS_ThrowTypeError(cx, "key size and \"alg\" value \"%s\" mismatch",
                              alg.start);
            JS_FreeCString(cx, (char *) alg.start);
            return JS_EXCEPTION;
        }
    }

    JS_FreeCString(cx, (char *) alg.start);

    val = JS_GetPropertyStr(cx, jwk, "key_ops");
    if (!JS_IsException(val) && !JS_IsUndefined(val)) {
        ret = qjs_key_usage(cx, val, &usage);
        JS_FreeValue(cx, val);
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if ((key->usage & usage) != key->usage) {
            JS_ThrowTypeError(cx, "Key operations and usage mismatch");
            return JS_EXCEPTION;
        }
    }

    if (key->extractable) {
        val = JS_GetPropertyStr(cx, jwk, "ext");
        if (!JS_IsException(val)
            && !JS_IsUndefined(val)
            && !JS_ToBool(cx, val))
        {
            JS_FreeValue(cx, val);
            JS_ThrowTypeError(cx, "JWK oct is not extractable");
            return JS_EXCEPTION;
        }

        JS_FreeValue(cx, val);
    }

    return JS_UNDEFINED;
}


static JSValue
qjs_webcrypto_import_key(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int                         nid;
    BIO                         *bio;
#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
    RSA                         *rsa;
    EC_KEY                      *ec;
#else
    char                        gname[80];
#endif
    JSValue                     options, key, jwk, val, ret;
    unsigned                    mask, usage;
    EVP_PKEY                    *pkey;
    njs_str_t                   key_data;
    const u_char                *start;
#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
    const EC_GROUP              *group;
#endif
    qjs_webcrypto_key_t         *wkey;
    PKCS8_PRIV_KEY_INFO         *pkcs8;
    qjs_webcrypto_hash_t        hash;
    qjs_webcrypto_jwk_kty_t     kty;
    qjs_webcrypto_algorithm_t   *alg;
    qjs_webcrypto_key_format_t  fmt;

    pkey = NULL;
    key_data.start = NULL;
    key_data.length = 0;

    fmt = qjs_key_format(cx, argv[0]);
    if (fmt == QJS_KEY_FORMAT_UNKNOWN) {
        return JS_EXCEPTION;
    }

    options = argv[2];

    alg = qjs_key_algorithm(cx, options);
    if (alg == NULL) {
        return JS_EXCEPTION;
    }

    if (!(fmt & alg->fmt)) {
        JS_ThrowTypeError(cx, "unsupported key fmt \"%s\" for \"%s\" key",
                          qjs_format_string(fmt), qjs_algorithm_string(alg));
        return JS_EXCEPTION;
    }

    ret = qjs_key_usage(cx, argv[4], &usage);
    if (JS_IsException(ret)) {
        return JS_EXCEPTION;
    }

    if (usage & ~alg->usage) {
        JS_ThrowTypeError(cx, "unsupported key usage for \"%s\" key",
                          qjs_algorithm_string(alg));
        return JS_EXCEPTION;
    }

    if (fmt != QJS_KEY_FORMAT_JWK) {
        ret = qjs_typed_array_data(cx, argv[1], &key_data);
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }
    }

    key = qjs_webcrypto_key_make(cx, alg, usage, JS_ToBool(cx, argv[3]));
    if (JS_IsException(key)) {
        return JS_EXCEPTION;
    }

    wkey = JS_GetOpaque(key, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);

    /*
     * set by qjs_webcrypto_key_make():
     *
     *  key->u.a.pkey = NULL;
     *  key->u.s.raw.length = 0;
     *  key->u.s.raw.start = NULL;
     *  key->u.a.curve = 0;
     *  key->u.a.privat = 0;
     *  key->hash = QJS_HASH_UNSET;
     */

    switch (fmt) {
    case QJS_KEY_FORMAT_PKCS8:
        bio = BIO_new_mem_buf(key_data.start, key_data.length);
        if (bio == NULL) {
            qjs_webcrypto_error(cx, "BIO_new_mem_buf() failed");
            goto fail;
        }

        pkcs8 = d2i_PKCS8_PRIV_KEY_INFO_bio(bio, NULL);
        if (pkcs8 == NULL) {
            BIO_free(bio);
            qjs_webcrypto_error(cx, "d2i_PKCS8_PRIV_KEY_INFO_bio() failed");
            goto fail;
        }

        pkey = EVP_PKCS82PKEY(pkcs8);
        if (pkey == NULL) {
            PKCS8_PRIV_KEY_INFO_free(pkcs8);
            BIO_free(bio);
            qjs_webcrypto_error(cx, "EVP_PKCS82PKEY() failed");
            goto fail;
        }

        PKCS8_PRIV_KEY_INFO_free(pkcs8);
        BIO_free(bio);

        wkey->u.a.privat = 1;

        break;

    case QJS_KEY_FORMAT_SPKI:
        start = key_data.start;
        pkey = d2i_PUBKEY(NULL, &start, key_data.length);
        if (pkey == NULL) {
            qjs_webcrypto_error(cx, "d2i_PUBKEY() failed");
            goto fail;
        }

        break;

    case QJS_KEY_FORMAT_JWK:
        jwk = argv[1];
        if (!JS_IsObject(jwk)) {
            JS_ThrowTypeError(cx, "invalid JWK key data: object value "
                              "expected");
            goto fail;
        }

        val = JS_GetPropertyStr(cx, jwk, "kty");
        if (JS_IsException(val)) {
            goto fail;
        }

        kty = qjs_jwk_kty(cx, val);
        JS_FreeValue(cx, val);
        if (kty == QJS_KEY_JWK_KTY_UNKNOWN) {
            goto fail;
        }

        switch (kty) {
        case QJS_KEY_JWK_KTY_RSA:
            pkey = qjs_import_jwk_rsa(cx, jwk, wkey);
            if (pkey == NULL) {
                goto fail;
            }

            break;

        case QJS_KEY_JWK_KTY_EC:
            ret = qjs_algorithm_curve(cx, options, &wkey->u.a.curve);
            if (JS_IsException(ret)) {
                goto fail;
            }

            pkey = qjs_import_jwk_ec(cx, jwk, wkey);
            if (pkey == NULL) {
                goto fail;
            }

            break;

        case QJS_KEY_JWK_KTY_OCT:
        default:
            ret = qjs_import_jwk_oct(cx, jwk, wkey);
            if (JS_IsException(ret)) {
                goto fail;
            }
        }

        break;

    case QJS_KEY_FORMAT_RAW:
    default:
        break;
    }

    switch (alg->type) {
    case QJS_ALGORITHM_RSA_OAEP:
    case QJS_ALGORITHM_RSA_PSS:
    case QJS_ALGORITHM_RSASSA_PKCS1_v1_5:

#if (OPENSSL_VERSION_NUMBER < 0x30000000L)

        rsa = EVP_PKEY_get1_RSA(pkey);
        if (rsa == NULL) {
            qjs_webcrypto_error(cx, "RSA key is not found");
            goto fail;
        }

        RSA_free(rsa);

#else
        if (!EVP_PKEY_is_a(pkey, "RSA")) {
            qjs_webcrypto_error(cx, "RSA key is not found");
            goto fail;
        }
#endif

        ret = qjs_algorithm_hash(cx, options, &hash);
        if (JS_IsException(ret)) {
            goto fail;
        }

        if (wkey->hash != QJS_HASH_UNSET && wkey->hash != hash) {
            JS_ThrowTypeError(cx, "RSA JWK hash mismatch");
            goto fail;
        }

        if (wkey->u.a.privat) {
            mask = (alg->type == QJS_ALGORITHM_RSA_OAEP)
                         ? ~(QJS_KEY_USAGE_DECRYPT | QJS_KEY_USAGE_UNWRAP_KEY)
                         : ~(QJS_KEY_USAGE_SIGN);
        } else {
            mask = (alg->type == QJS_ALGORITHM_RSA_OAEP)
                         ? ~(QJS_KEY_USAGE_ENCRYPT | QJS_KEY_USAGE_WRAP_KEY)
                         : ~(QJS_KEY_USAGE_VERIFY);
        }

        if (wkey->usage & mask) {
            JS_ThrowTypeError(cx, "key usage mismatch for \"%s\" key",
                              qjs_algorithm_string(alg));
            goto fail;
        }

        wkey->hash = hash;
        wkey->u.a.pkey = pkey;

        break;

    case QJS_ALGORITHM_ECDSA:
    case QJS_ALGORITHM_ECDH:
        ret = qjs_algorithm_curve(cx, options, &wkey->u.a.curve);
        if (JS_IsException(ret)) {
            goto fail;
        }

        if (fmt == QJS_KEY_FORMAT_RAW) {
            pkey = qjs_import_raw_ec(cx, &key_data, wkey);
            if (pkey == NULL) {
                goto fail;
            }
        }

#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
        ec = EVP_PKEY_get1_EC_KEY(pkey);
        if (ec == NULL) {
            qjs_webcrypto_error(cx, "EC key is not found");
            goto fail;
        }

        group = EC_KEY_get0_group(ec);
        nid = EC_GROUP_get_curve_name(group);
        EC_KEY_free(ec);
#else
        if (!EVP_PKEY_is_a(pkey, "EC")) {
            qjs_webcrypto_error(cx, "EC key is not found");
            goto fail;
        }

        if (EVP_PKEY_get_group_name(pkey, gname, sizeof(gname), NULL) != 1) {
            qjs_webcrypto_error(cx, "EVP_PKEY_get_group_name() failed");
            goto fail;
        }

        nid = OBJ_txt2nid(gname);
#endif

        if (wkey->u.a.curve != nid) {
            qjs_webcrypto_error(cx, "name curve mismatch");
            goto fail;
        }

        mask = wkey->u.a.privat ? ~QJS_KEY_USAGE_SIGN : ~QJS_KEY_USAGE_VERIFY;

        if (wkey->usage & mask) {
            JS_ThrowTypeError(cx, "key usage mismatch for \"%s\" key",
                              qjs_algorithm_string(alg));
            goto fail;
        }

        wkey->u.a.pkey = pkey;
        break;

    case QJS_ALGORITHM_HMAC:
        if (fmt == QJS_KEY_FORMAT_RAW) {
            ret = qjs_algorithm_hash(cx, options, &wkey->hash);
            if (JS_IsException(ret)) {
                goto fail;
            }

            wkey->u.s.raw.start = js_malloc(cx, key_data.length);
            if (wkey->u.s.raw.start == NULL) {
                JS_ThrowOutOfMemory(cx);
                goto fail;
            }

            wkey->u.s.raw.length = key_data.length;
            memcpy(wkey->u.s.raw.start, key_data.start, key_data.length);

        } else {
            /* QJS_KEY_FORMAT_JWK. */

            ret = qjs_algorithm_hash(cx, options, &hash);
            if (JS_IsException(ret)) {
                goto fail;
            }

            if (wkey->hash != QJS_HASH_UNSET && wkey->hash != hash) {
                JS_ThrowTypeError(cx, "HMAC JWK hash mismatch");
                goto fail;
            }
        }

        break;

    case QJS_ALGORITHM_AES_GCM:
    case QJS_ALGORITHM_AES_CTR:
    case QJS_ALGORITHM_AES_CBC:
        if (fmt == QJS_KEY_FORMAT_RAW) {
            switch (key_data.length) {
            case 16:
            case 24:
            case 32:
                break;

            default:
                JS_ThrowTypeError(cx, "AES Invalid key length");
                goto fail;
            }

            wkey->u.s.raw.start = js_malloc(cx, key_data.length);
            if (wkey->u.s.raw.start == NULL) {
                JS_ThrowOutOfMemory(cx);
                goto fail;
            }

            wkey->u.s.raw.length = key_data.length;
            memcpy(wkey->u.s.raw.start, key_data.start, key_data.length);
        }

        break;

    case QJS_ALGORITHM_PBKDF2:
    case QJS_ALGORITHM_HKDF:
    default:
        wkey->u.s.raw.start = js_malloc(cx, key_data.length);
        if (wkey->u.s.raw.start == NULL) {
            JS_ThrowOutOfMemory(cx);
            goto fail;
        }

        wkey->u.s.raw.length = key_data.length;
        memcpy(wkey->u.s.raw.start, key_data.start, key_data.length);
        break;
    }

    return qjs_webcrypto_result(cx, key, 0);

fail:

    if (pkey != NULL) {
        EVP_PKEY_free(pkey);
    }

    JS_FreeValue(cx, key);

    return qjs_webcrypto_result(cx, JS_UNDEFINED, -1);
}


static int
qjs_set_rsa_padding(JSContext *cx, JSValue options, EVP_PKEY *pkey,
    EVP_PKEY_CTX *ctx, qjs_webcrypto_alg_t type)
{
    int      padding, rc;
    int64_t  salt_length;
    JSValue  value;

    if (type == QJS_ALGORITHM_ECDSA) {
        return 0;
    }

    padding = (type == QJS_ALGORITHM_RSA_PSS) ? RSA_PKCS1_PSS_PADDING
                                              : RSA_PKCS1_PADDING;
    rc = EVP_PKEY_CTX_set_rsa_padding(ctx, padding);
    if (rc <= 0) {
        qjs_webcrypto_error(cx, "EVP_PKEY_CTX_set_rsa_padding() failed");
        return -1;
    }

    if (padding == RSA_PKCS1_PSS_PADDING) {
        value = JS_GetPropertyStr(cx, options, "saltLength");
        if (JS_IsException(value)) {
            return -1;
        }

        if (JS_IsUndefined(value)) {
            JS_ThrowTypeError(cx, "RSA-PSS algorithm.saltLength is not "
                              "provided");
            return -1;
        }

        rc = JS_ToInt64(cx, &salt_length, value);
        JS_FreeValue(cx, value);
        if (rc < 0) {
            return -1;
        }

        rc = EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx, salt_length);
        if (rc <= 0) {
            qjs_webcrypto_error(cx,
                                "EVP_PKEY_CTX_set_rsa_pss_saltlen() failed");
            return -1;
        }
    }

    return 0;
}


static unsigned int
qjs_ec_rs_size(EVP_PKEY *pkey)
{
    int             bits;
    const EC_KEY    *ec_key;
    const EC_GROUP  *ec_group;

    ec_key = njs_pkey_get_ec_key(pkey);
    if (ec_key == NULL) {
        return 0;
    }

    ec_group = EC_KEY_get0_group(ec_key);
    if (ec_group == NULL) {
        return 0;
    }

    bits = njs_ec_group_order_bits(ec_group);
    if (bits == 0) {
        return 0;
    }

    return (bits + 7) / 8;
}


static int
qjs_convert_der_to_p1363(JSContext *cx, EVP_PKEY *pkey, const u_char *der,
    size_t der_len, u_char **pout, size_t *out_len)
{
    u_char        *data;
    unsigned      n;
    ECDSA_SIG     *ec_sig;
    const BIGNUM  *r, *s;

    ec_sig = NULL;

    n = qjs_ec_rs_size(pkey);
    if (n == 0) {
        return -1;
    }

    data = js_malloc(cx, 2 * n);
    if (data == NULL) {
        JS_ThrowOutOfMemory(cx);
        return -1;
    }

    ec_sig = d2i_ECDSA_SIG(NULL, &der, der_len);
    if (ec_sig == NULL) {
        goto fail;
    }

#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
    ECDSA_SIG_get0(ec_sig, &r, &s);
#else
    r = ec_sig->r;
    s = ec_sig->s;
#endif

    if (BN_bn2binpad(r, data, n) <= 0) {
        goto fail;
    }

    if (BN_bn2binpad(s, &data[n], n) <= 0) {
        goto fail;
    }

    *pout = data;
    *out_len = 2 * n;

    ECDSA_SIG_free(ec_sig);

    return 0;

fail:

    js_free(cx, data);

    if (ec_sig != NULL) {
        ECDSA_SIG_free(ec_sig);
    }

    return -1;
}


static int
qjs_convert_p1363_to_der(JSContext *cx, EVP_PKEY *pkey, u_char *p1363,
    size_t p1363_len, u_char **pout, size_t *out_len)
{
    int        len;
    BIGNUM     *r, *s;
    u_char     *data;
    unsigned   n;
    ECDSA_SIG  *ec_sig;

    n = qjs_ec_rs_size(pkey);

    if (n == 0 || p1363_len != 2 * n) {
        JS_ThrowTypeError(cx, "invalid ECDSA signature length %zu != %u",
                          p1363_len, 2 * n);
        return -1;
    }

    ec_sig = ECDSA_SIG_new();
    if (ec_sig == NULL) {
        JS_ThrowOutOfMemory(cx);
        return -1;
    }

    r = BN_bin2bn(p1363, n, NULL);
    if (r == NULL) {
        JS_ThrowOutOfMemory(cx);
        goto fail;
    }

    s = BN_bin2bn(&p1363[n], n, NULL);
    if (s == NULL) {
        BN_free(r);
        JS_ThrowOutOfMemory(cx);
        goto fail;
    }

    if (ECDSA_SIG_set0(ec_sig, r, s) != 1) {
        BN_free(r);
        BN_free(s);
        JS_ThrowOutOfMemory(cx);
        goto fail;
    }

    data = js_malloc(cx, 2 * n + 16);
    if (data == NULL) {
        JS_ThrowOutOfMemory(cx);
        goto fail;
    }

    *pout = data;
    len = i2d_ECDSA_SIG(ec_sig, &data);

    if (len < 0) {
        js_free(cx, data);
        qjs_webcrypto_error(cx, "i2d_ECDSA_SIG() failed");
        goto fail;
    }

    *out_len = len;

    ECDSA_SIG_free(ec_sig);

    return 0;

fail:

    ECDSA_SIG_free(ec_sig);

    return -1;
}


static JSValue
qjs_webcrypto_sign(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv, int verify)
{
    int                        rc;
    u_char                     *dst, *p, *p1363;
    size_t                     olen, outlen;
    JSValue                    ret, options;
    unsigned                   mask, m_len;
    njs_str_t                  data, sig;
    EVP_MD_CTX                 *mctx;
    EVP_PKEY_CTX               *pctx;
    const EVP_MD               *md;
    qjs_webcrypto_key_t        *key;
    qjs_webcrypto_hash_t       hash;
    qjs_webcrypto_algorithm_t  *alg;
    unsigned char              m[EVP_MAX_MD_SIZE];

    dst = NULL;
    mctx = NULL;
    pctx = NULL;

    options = argv[0];

    alg = qjs_key_algorithm(cx, options);
    if (alg == NULL) {
        return JS_EXCEPTION;
    }

    key = JS_GetOpaque2(cx, argv[1], QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    if (key == NULL) {
        JS_ThrowTypeError(cx, "\"key\" is not a CryptoKey object");
        return JS_EXCEPTION;
    }

    mask = verify ? QJS_KEY_USAGE_VERIFY : QJS_KEY_USAGE_SIGN;
    if (!(key->usage & mask)) {
        JS_ThrowTypeError(cx, "provide key does not support \"%s\" operation",
                          verify ? "verify" : "sign");
        return JS_EXCEPTION;
    }

    if (key->alg != alg) {
        JS_ThrowTypeError(cx, "cannot %s using \"%s\" with \"%s\" key",
                          verify ? "verify" : "sign",
                          qjs_algorithm_string(key->alg),
                          qjs_algorithm_string(alg));
        return JS_EXCEPTION;
    }

    if (verify) {
        ret = qjs_typed_array_data(cx, argv[2], &sig);
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        ret = qjs_typed_array_data(cx, argv[3], &data);
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

    } else {
        ret = qjs_typed_array_data(cx, argv[2], &data);
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }
    }

    if (alg->type == QJS_ALGORITHM_ECDSA) {
        ret = qjs_algorithm_hash(cx, options, &hash);
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

    } else {
        hash = key->hash;
    }

    md = qjs_algorithm_hash_digest(hash);

    /* Clang complains about uninitialized rc. */
    rc = 0;
    outlen = 0;

    switch (alg->type) {
    case QJS_ALGORITHM_HMAC:
        m_len = EVP_MD_size(md);

        if (!verify) {
            dst = js_malloc(cx, m_len);
            if (dst == NULL) {
                JS_ThrowOutOfMemory(cx);
                return JS_EXCEPTION;
            }

        } else {
            dst = (u_char *) &m[0];
        }

        outlen = m_len;

        p = HMAC(md, key->u.s.raw.start, key->u.s.raw.length, data.start,
                 data.length, dst, &m_len);

        if (p == NULL || m_len != outlen) {
            qjs_webcrypto_error(cx, "HMAC() failed");
            goto fail;
        }

        if (verify) {
            rc = (sig.length == outlen && memcmp(sig.start, dst, outlen) == 0);
        }

        break;

    case QJS_ALGORITHM_RSASSA_PKCS1_v1_5:
    case QJS_ALGORITHM_RSA_PSS:
    case QJS_ALGORITHM_ECDSA:
    default:
        mctx = njs_evp_md_ctx_new();
        if (mctx == NULL) {
            qjs_webcrypto_error(cx, "njs_evp_md_ctx_new() failed");
            goto fail;
        }

        rc = EVP_DigestInit_ex(mctx, md, NULL);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "EVP_DigestInit_ex() failed");
            goto fail;
        }

        rc = EVP_DigestUpdate(mctx, data.start, data.length);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "EVP_DigestUpdate() failed");
            goto fail;
        }

        rc = EVP_DigestFinal_ex(mctx, m, &m_len);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "EVP_DigestFinal_ex() failed");
            goto fail;
        }

        olen = EVP_PKEY_size(key->u.a.pkey);
        dst = js_malloc(cx, olen);
        if (dst == NULL) {
            JS_ThrowOutOfMemory(cx);
            goto fail;
        }

        pctx = EVP_PKEY_CTX_new(key->u.a.pkey, NULL);
        if (pctx == NULL) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_new() failed");
            goto fail;
        }

        if (!verify) {
            rc = EVP_PKEY_sign_init(pctx);
            if (rc <= 0) {
                qjs_webcrypto_error(cx, "EVP_PKEY_sign_init() failed");
                goto fail;
            }

        } else {
            rc = EVP_PKEY_verify_init(pctx);
            if (rc <= 0) {
                qjs_webcrypto_error(cx, "EVP_PKEY_verify_init() failed");
                goto fail;
            }
        }

        rc = qjs_set_rsa_padding(cx, options, key->u.a.pkey, pctx, alg->type);
        if (rc < 0) {
            goto fail;
        }

        rc = EVP_PKEY_CTX_set_signature_md(pctx, md);
        if (rc <= 0) {
            qjs_webcrypto_error(cx, "EVP_PKEY_CTX_set_signature_md() failed");
            goto fail;
        }

        if (!verify) {
            outlen = olen;
            rc = EVP_PKEY_sign(pctx, dst, &outlen, m, m_len);
            if (rc <= 0) {
                qjs_webcrypto_error(cx, "EVP_PKEY_sign() failed");
                goto fail;
            }

            if (alg->type == QJS_ALGORITHM_ECDSA) {
                rc = qjs_convert_der_to_p1363(cx, key->u.a.pkey, dst, outlen,
                                              &p1363, &outlen);
                if (rc < 0) {
                    goto fail;
                }

                js_free(cx, dst);
                dst = p1363;
            }

        } else {
            if (alg->type == QJS_ALGORITHM_ECDSA) {
                rc = qjs_convert_p1363_to_der(cx, key->u.a.pkey, sig.start,
                                              sig.length, &sig.start,
                                              &sig.length);
                if (rc < 0) {
                    goto fail;
                }
            }

            rc = EVP_PKEY_verify(pctx, sig.start, sig.length, m, m_len);

            if (alg->type == QJS_ALGORITHM_ECDSA) {
                js_free(cx, sig.start);
            }

            if (rc < 0) {
                qjs_webcrypto_error(cx, "EVP_PKEY_verify() failed");
                goto fail;
            }

            js_free(cx, dst);
        }

        njs_evp_md_ctx_free(mctx);
        mctx = NULL;

        EVP_PKEY_CTX_free(pctx);
        pctx = NULL;
    }

    if (!verify) {
        ret = qjs_new_array_buffer(cx, dst, outlen);
        if (JS_IsException(ret)) {
            goto fail;
        }

    } else {
        ret = JS_NewBool(cx, rc != 0);
    }

    return qjs_webcrypto_result(cx, ret, 0);

fail:

    if (mctx != NULL) {
        njs_evp_md_ctx_free(mctx);
    }

    if (pctx != NULL) {
        EVP_PKEY_CTX_free(pctx);
    }

    if (dst != NULL) {
        js_free(cx, dst);
    }

    return qjs_webcrypto_result(cx, JS_UNDEFINED, -1);
}


static JSValue
qjs_webcrypto_key_algorithm(JSContext *cx, JSValueConst this_val)
{
    JSValue              obj, ret, hash, len, pe;
    njs_str_t            *name, pe_data;
    const BIGNUM         *n_bn, *e_bn;
    const EC_GROUP       *group;
    qjs_webcrypto_key_t  *key;

    key = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    if (key == NULL) {
        JS_ThrowTypeError(cx, "\"key\" is not a CryptoKey object");
        return JS_EXCEPTION;
    }

    name = &qjs_webcrypto_alg[key->alg->type].name;

    obj = JS_NewObject(cx);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    ret = JS_NewStringLen(cx, (const char *) name->start, name->length);
    if (JS_IsException(ret)) {
        JS_FreeValue(cx, obj);
        return JS_EXCEPTION;
    }

    if (JS_DefinePropertyValueStr(cx, obj, "name", ret, JS_PROP_C_W_E)
        < 0)
    {
        JS_FreeValue(cx, obj);
        return JS_EXCEPTION;
    }

    switch (key->alg->type) {
    case QJS_ALGORITHM_RSASSA_PKCS1_v1_5:
    case QJS_ALGORITHM_RSA_PSS:
    case QJS_ALGORITHM_RSA_OAEP:
        /* RsaHashedKeyGenParams. */

        njs_assert(key->u.a.pkey != NULL);
        njs_assert(EVP_PKEY_id(key->u.a.pkey) == EVP_PKEY_RSA);

        njs_rsa_get0_key(njs_pkey_get_rsa_key(key->u.a.pkey), &n_bn, &e_bn,
                         NULL);

        if (JS_DefinePropertyValueStr(cx, obj, "modulusLength",
                                      JS_NewInt32(cx, BN_num_bits(n_bn)),
                                                  JS_PROP_C_W_E)
            < 0)
        {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        len = JS_NewInt32(cx, BN_num_bytes(e_bn));
        pe = qjs_new_uint8_array(cx, 1, &len);
        if (JS_IsException(pe)) {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        ret = qjs_typed_array_data(cx, pe, &pe_data);
        if (JS_IsException(ret)) {
            JS_FreeValue(cx, pe);
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        BN_bn2bin(e_bn, pe_data.start);

        if (JS_DefinePropertyValueStr(cx, obj, "publicExponent", pe,
                                      JS_PROP_C_W_E)
            < 0)
        {
            JS_FreeValue(cx, pe);
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        hash = JS_NewString(cx, qjs_algorithm_hash_name(key->hash));
        if (JS_IsException(hash)) {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        ret = JS_NewObject(cx);
        if (JS_IsException(ret)) {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueStr(cx, ret, "name", hash, JS_PROP_C_W_E)
            < 0)
        {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueStr(cx, obj, "hash", ret, JS_PROP_C_W_E)
            < 0)
        {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        break;

    case QJS_ALGORITHM_AES_GCM:
    case QJS_ALGORITHM_AES_CTR:
    case QJS_ALGORITHM_AES_CBC:
        /* AesKeyGenParams. */

        if (JS_DefinePropertyValueStr(cx, obj, "length",
                                      JS_NewInt32(cx, key->u.s.raw.length * 8),
                                      JS_PROP_C_W_E)
            < 0)
        {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        break;

    case QJS_ALGORITHM_ECDSA:
    case QJS_ALGORITHM_ECDH:
        /* EcKeyGenParams. */

        njs_assert(key->u.a.pkey != NULL);
        njs_assert(EVP_PKEY_id(key->u.a.pkey) == EVP_PKEY_EC);

        group = EC_KEY_get0_group(njs_pkey_get_ec_key(key->u.a.pkey));
        name = qjs_algorithm_curve_name(EC_GROUP_get_curve_name(group));

        ret = JS_NewStringLen(cx, (const char *) name->start, name->length);
        if (JS_IsException(ret)) {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueStr(cx, obj, "namedCurve", ret, JS_PROP_C_W_E)
            < 0)
        {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        break;

    case QJS_ALGORITHM_HMAC:
    default:
         /* HmacKeyGenParams */

        hash = JS_NewString(cx, qjs_algorithm_hash_name(key->hash));
        if (JS_IsException(hash)) {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        if (JS_DefinePropertyValueStr(cx, obj, "hash", hash, JS_PROP_C_W_E)
            < 0)
        {
            JS_FreeValue(cx, obj);
            return JS_EXCEPTION;
        }

        break;
    }

    return obj;
}


static JSValue
qjs_webcrypto_key_extractable(JSContext *cx, JSValueConst this_val)
{
    qjs_webcrypto_key_t  *key;

    key = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    if (key == NULL) {
        JS_ThrowTypeError(cx, "\"key\" is not a CryptoKey object");
        return JS_EXCEPTION;
    }

    return JS_NewBool(cx, key->extractable);
}


static JSValue
qjs_webcrypto_key_type(JSContext *cx, JSValueConst this_val)
{
    qjs_webcrypto_key_t  *key;

    key = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    if (key == NULL) {
        JS_ThrowTypeError(cx, "\"key\" is not a CryptoKey object");
        return JS_EXCEPTION;
    }

    if (key->alg->raw) {
        return JS_NewString(cx, "secret");
    }

    return JS_NewString(cx, key->u.a.privat ? "private": "public");
}


static JSValue
qjs_webcrypto_key_usages(JSContext *cx, JSValueConst this_val)
{
    qjs_webcrypto_key_t  *key;

    key = JS_GetOpaque2(cx, this_val, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    if (key == NULL) {
        JS_ThrowTypeError(cx, "\"key\" is not a CryptoKey object");
        return JS_EXCEPTION;
    }

    return qjs_key_ops(cx, key->usage);
}


static JSValue
qjs_get_random_values(JSContext *cx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue    buffer, ret;
    njs_str_t  fill;

    buffer = JS_DupValue(cx, argv[0]);

    ret = qjs_typed_array_data(cx, buffer, &fill);
    if (JS_IsException(ret)) {
        JS_FreeValue(cx, buffer);
        return JS_EXCEPTION;
    }

    if (fill.length > 65536) {
        JS_ThrowTypeError(cx, "requested length exceeds 65536 bytes");
        JS_FreeValue(cx, buffer);
        return JS_EXCEPTION;
    }

    if (RAND_bytes(fill.start, fill.length) != 1) {
        JS_FreeValue(cx, buffer);
        qjs_webcrypto_error(cx, "RAND_bytes() failed");
        return JS_EXCEPTION;
    }

    return buffer;
}


static JSValue
qjs_webcrypto_key_make(JSContext *cx, qjs_webcrypto_algorithm_t *alg,
  unsigned usage, int extractable)
{
    JSValue              key;
    qjs_webcrypto_key_t  *k;

    key = JS_NewObjectClass(cx, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);
    if (JS_IsException(key)) {
        return JS_EXCEPTION;
    }

    k = js_mallocz(cx, sizeof(qjs_webcrypto_key_t));
    if (k == NULL) {
        return JS_ThrowOutOfMemory(cx);
    }

    /*
     *  k->u.a.pkey = NULL;
     *  k->u.s.raw.length = 0;
     *  k->u.s.raw.start = NULL;
     *  k->u.a.curve = 0;
     *  k->u.a.privat = 0;
     *  k->hash = QJS_HASH_UNSET;
     */

    k->alg = alg;
    k->usage = usage;
    k->extractable = extractable;

    JS_SetOpaque(key, k);

    return key;
}


static void
qjs_webcrypto_key_finalizer(JSRuntime *rt, JSValue val)
{
    qjs_webcrypto_key_t  *key;

    key = JS_GetOpaque(val, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY);

    if (key != NULL) {
        if (!key->alg->raw) {
            if (key->u.a.pkey != NULL) {
                EVP_PKEY_free(key->u.a.pkey);
            }

        } else {
            if (key->u.s.raw.start != NULL) {
                js_free_rt(rt, key->u.s.raw.start);
            }
        }

        js_free_rt(rt, key);
    }
}


static qjs_webcrypto_key_format_t
qjs_key_format(JSContext *cx, JSValueConst value)
{
    njs_str_t              format;
    qjs_webcrypto_entry_t  *e;

    format.start = (u_char *) JS_ToCStringLen(cx, &format.length, value);
    if (format.start == NULL) {
        return QJS_KEY_FORMAT_UNKNOWN;
    }

    for (e = &qjs_webcrypto_format[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&format, &e->name)) {
            JS_FreeCString(cx, (char *) format.start);
            return e->value;
        }
    }

    JS_ThrowTypeError(cx, "unknown key format: \"%s\"", format.start);
    JS_FreeCString(cx, (char *) format.start);

    return QJS_KEY_FORMAT_UNKNOWN;
}


static qjs_webcrypto_jwk_kty_t
qjs_jwk_kty(JSContext *cx, JSValueConst value)
{
    njs_str_t              kty;
    qjs_webcrypto_entry_t  *e;

    kty.start = (u_char *) JS_ToCStringLen(cx, &kty.length, value);
    if (kty.start == NULL) {
        return QJS_KEY_JWK_KTY_UNKNOWN;
    }

    for (e = &qjs_webcrypto_jwk_kty[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&kty, &e->name)) {
            JS_FreeCString(cx, (char *) kty.start);
            return e->value;
        }
    }

    JS_ThrowTypeError(cx, "invalid JWK key type: \"%s\"", kty.start);
    JS_FreeCString(cx, (char *) kty.start);

    return QJS_KEY_JWK_KTY_UNKNOWN;
}


static qjs_webcrypto_algorithm_t *
qjs_key_algorithm(JSContext *cx, JSValue options)
{
    JSValue                    v;
    njs_str_t                  a;
    qjs_webcrypto_entry_t      *e;
    qjs_webcrypto_algorithm_t  *alg;

    if (JS_IsObject(options)) {
        v = JS_GetPropertyStr(cx, options, "name");
        if (JS_IsException(v)) {
            return NULL;
        }

    } else {
        v = JS_DupValue(cx, options);
    }

    a.start = (u_char *) JS_ToCStringLen(cx, &a.length, v);
    JS_FreeValue(cx, v);
    if (a.start == NULL) {
        return NULL;
    }

    for (e = &qjs_webcrypto_alg[0]; e->name.length != 0; e++) {
        if (njs_strstr_case_eq(&a, &e->name)) {
            alg = (qjs_webcrypto_algorithm_t *) e->value;
            if (alg->usage & QJS_KEY_USAGE_UNSUPPORTED) {
                JS_ThrowTypeError(cx, "unsupported algorithm: \"%.*s\"",
                                  (int) a.length, a.start);
                JS_FreeCString(cx, (char *) a.start);
                return NULL;
            }

            JS_FreeCString(cx, (char *) a.start);
            return alg;
        }
    }

    JS_ThrowTypeError(cx, "unknown algorithm name: \"%.*s\"", (int) a.length,
                      a.start);
    JS_FreeCString(cx, (char *) a.start);

    return NULL;
}


static JSValue
qjs_algorithm_curve(JSContext *cx, JSValue options, int *curve)
{
    JSValue                v;
    njs_str_t              name;
    qjs_webcrypto_entry_t  *e;

    if (JS_IsObject(options)) {
        v = JS_GetPropertyStr(cx, options, "namedCurve");
        if (JS_IsException(v)) {
            return JS_EXCEPTION;
        }

    } else {
        v = JS_DupValue(cx, options);
    }

    name.start = (u_char *) JS_ToCStringLen(cx, &name.length, v);
    JS_FreeValue(cx, v);
    if (name.start == NULL) {
        return JS_EXCEPTION;
    }

    for (e = &qjs_webcrypto_curve[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&name, &e->name)) {
            JS_FreeCString(cx, (char *) name.start);
            *curve = e->value;
            return JS_UNDEFINED;
        }
    }

    JS_ThrowTypeError(cx, "unknown namedCurve: \"%.*s\"", (int) name.length,
                      name.start);
    JS_FreeCString(cx, (char *) name.start);

    return JS_EXCEPTION;
}


static JSValue
qjs_algorithm_hash(JSContext *cx, JSValue options, qjs_webcrypto_hash_t *hash)
{
    JSValue                v;
    njs_str_t              name;
    qjs_webcrypto_entry_t  *e;

    if (JS_IsObject(options)) {
        v = JS_GetPropertyStr(cx, options, "hash");
        if (JS_IsException(v)) {
            return JS_EXCEPTION;
        }

    } else {
        v = JS_DupValue(cx, options);
    }

    name.start = (u_char *) JS_ToCStringLen(cx, &name.length, v);
    JS_FreeValue(cx, v);
    if (name.start == NULL) {
        return JS_EXCEPTION;
    }

    for (e = &qjs_webcrypto_hash[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&name, &e->name)) {
            JS_FreeCString(cx, (char *) name.start);
            *hash = e->value;
            return JS_UNDEFINED;
        }
    }

    JS_ThrowTypeError(cx, "unknown hash name: \"%.*s\"", (int) name.length,
                      name.start);
    JS_FreeCString(cx, (char *) name.start);

    return JS_EXCEPTION;
}


static const EVP_MD *
qjs_algorithm_hash_digest(qjs_webcrypto_hash_t hash)
{
    switch (hash) {
    case QJS_HASH_SHA256:
        return EVP_sha256();

    case QJS_HASH_SHA384:
        return EVP_sha384();

    case QJS_HASH_SHA512:
        return EVP_sha512();

    case QJS_HASH_SHA1:
    default:
        break;
    }

    return EVP_sha1();
}


static njs_str_t *
qjs_algorithm_curve_name(int curve)
{
    qjs_webcrypto_entry_t  *e;

    for (e = &qjs_webcrypto_curve[0]; e->name.length != 0; e++) {
        if (e->value == (uintptr_t) curve) {
            return &e->name;
        }
    }

    return &e->name;
}


static const char *
qjs_format_string(qjs_webcrypto_key_format_t fmt)
{
    qjs_webcrypto_entry_t  *e;

    for (e = &qjs_webcrypto_format[0]; e->name.length != 0; e++) {
        if (fmt == e->value) {
            break;
        }
    }

    return (const char *) e->name.start;
}


static const char *
qjs_algorithm_string(qjs_webcrypto_algorithm_t *algorithm)
{
    qjs_webcrypto_entry_t      *e;
    qjs_webcrypto_algorithm_t  *alg;

    for (e = &qjs_webcrypto_alg[0]; e->name.length != 0; e++) {
        alg = (qjs_webcrypto_algorithm_t *) e->value;
        if (alg->type == algorithm->type) {
            break;
        }
    }

    return (const char *) e->name.start;
}


static const char *
qjs_algorithm_hash_name(qjs_webcrypto_hash_t hash)
{
    qjs_webcrypto_entry_t  *e;

    for (e = &qjs_webcrypto_hash[0]; e->name.length != 0; e++) {
        if (e->value == hash) {
            break;
        }
    }

    return (const char *) e->name.start;
}


static JSValue
qjs_key_usage(JSContext *cx, JSValue value, unsigned *mask)
{
    int64_t                length;
    JSValue                v;
    uint32_t               i;
    njs_str_t              s;
    qjs_webcrypto_entry_t  *e;

    if (!JS_IsArray(cx, value)) {
        JS_ThrowTypeError(cx, "\"keyUsages\" argument must be an Array");
        return JS_EXCEPTION;
    }

    v = JS_GetPropertyStr(cx, value, "length");
    if (JS_IsException(v)) {
        return JS_EXCEPTION;
    }

    if (JS_ToInt64(cx, &length, v) < 0) {
        JS_FreeValue(cx, v);
        return JS_EXCEPTION;
    }

    JS_FreeValue(cx, v);

    *mask = 0;

    for (i = 0; i < length; i++) {
        v = JS_GetPropertyUint32(cx, value, i);
        if (JS_IsException(v)) {
            return JS_EXCEPTION;
        }

        s.start = (u_char *) JS_ToCStringLen(cx, &s.length, v);
        JS_FreeValue(cx, v);
        if (s.start == NULL) {
            JS_ThrowOutOfMemory(cx);
            return JS_EXCEPTION;
        }

        for (e = &qjs_webcrypto_usage[0]; e->name.length != 0; e++) {
            if (njs_strstr_eq(&s, &e->name)) {
                *mask |= e->value;
                goto done;
            }
        }

        JS_ThrowTypeError(cx, "unknown key usage: \"%.*s\"", (int) s.length,
                          s.start);
        JS_FreeCString(cx, (char *) s.start);
        return JS_EXCEPTION;

done:

        JS_FreeCString(cx, (char *) s.start);
    }

    return JS_UNDEFINED;
}


static JSValue
qjs_key_ops(JSContext *cx, unsigned mask)
{
    uint32_t               i;
    JSValue                ops, value;
    qjs_webcrypto_entry_t  *e;

    ops = JS_NewArray(cx);
    if (JS_IsException(ops)) {
        return JS_EXCEPTION;
    }

    i = 0;

    for (e = &qjs_webcrypto_usage[0]; e->name.length != 0; e++) {
        if (mask & e->value) {
            value = JS_NewStringLen(cx, (const char *) e->name.start,
                                    e->name.length);
            if (JS_IsException(value)) {
                JS_FreeValue(cx, ops);
                return JS_EXCEPTION;
            }

            if (JS_SetPropertyUint32(cx, ops, i++, value) < 0) {
                JS_FreeValue(cx, ops);
                JS_FreeValue(cx, value);
                return JS_EXCEPTION;
            }
        }
    }

    return ops;
}


static u_char *
qjs_cpystrn(u_char *dst, u_char *src, size_t n)
{
    if (n == 0) {
        return dst;
    }

    while (--n) {
        *dst = *src;

        if (*dst == '\0') {
            return dst;
        }

        dst++;
        src++;
    }

    *dst = '\0';

    return dst;
}


static JSValue
qjs_webcrypto_promise_trampoline(JSContext *cx, int argc, JSValueConst *argv)
{
    return JS_Call(cx, argv[0], JS_UNDEFINED, 1, &argv[1]);
}


static JSValue
qjs_webcrypto_result(JSContext *cx, JSValue result, int rc)
{
    JS_BOOL  is_error;
    JSValue  promise, callbacks[2], arguments[2];

    promise = JS_NewPromiseCapability(cx, callbacks);
    if (JS_IsException(promise)) {
        JS_FreeValue(cx, result);
        return JS_EXCEPTION;
    }

    is_error = !!(rc != 0);

    JS_FreeValue(cx, callbacks[!is_error]);
    arguments[0] = callbacks[is_error];
    arguments[1] = is_error ? JS_GetException(cx) : result;

    if (JS_EnqueueJob(cx, qjs_webcrypto_promise_trampoline, 2, arguments) < 0) {
        JS_FreeValue(cx, promise);
        JS_FreeValue(cx, callbacks[is_error]);
        JS_FreeValue(cx, arguments[1]);
        return JS_EXCEPTION;
    }

    JS_FreeValue(cx, arguments[0]);
    JS_FreeValue(cx, arguments[1]);

    return promise;
}


static void
qjs_webcrypto_error(JSContext *cx, const char *fmt, ...)
{
    int            flags;
    u_char         *p, *last;
    va_list        args;
    const char     *data;
    unsigned long  n;
    u_char         errstr[NJS_MAX_ERROR_STR];

    last = &errstr[NJS_MAX_ERROR_STR];

    va_start(args, fmt);
    p = njs_vsprintf(errstr, last - 1, fmt, args);
    va_end(args);

    if (ERR_peek_error()) {
        p = qjs_cpystrn(p, (u_char *) " (SSL:", last - p);

        for ( ;; ) {

            n = ERR_peek_error_data(&data, &flags);

            if (n == 0) {
                break;
            }

            /* ERR_error_string_n() requires at least one byte */

            if (p >= last - 1) {
                goto next;
            }

            *p++ = ' ';

            ERR_error_string_n(n, (char *) p, last - p);

            while (p < last && *p) {
                p++;
            }

            if (p < last && *data && (flags & ERR_TXT_STRING)) {
                *p++ = ':';
                p = qjs_cpystrn(p, (u_char *) data, last - p);
            }

        next:

            (void) ERR_get_error();
        }

        if (p < last) {
            *p++ = ')';
        }
    }

    JS_ThrowTypeError(cx, "%.*s", (int) (p - errstr), errstr);
}


static int
qjs_webcrypto_module_init(JSContext *cx, JSModuleDef *m)
{
    int      rc;
    JSValue  proto;

    proto = JS_NewObject(cx);
    JS_SetPropertyFunctionList(cx, proto, qjs_webcrypto_export,
                               njs_nitems(qjs_webcrypto_export));

    rc = JS_SetModuleExport(cx, m, "default", proto);
    if (rc != 0) {
        return -1;
    }

    return JS_SetModuleExportList(cx, m, qjs_webcrypto_export,
                                  njs_nitems(qjs_webcrypto_export));
}


static JSModuleDef *
qjs_webcrypto_init(JSContext *cx, const char *name)
{
    int          rc;
    JSValue      crypto, proto, global_obj;
    JSModuleDef  *m;

    if (!JS_IsRegisteredClass(JS_GetRuntime(cx),
                              QJS_CORE_CLASS_ID_WEBCRYPTO_KEY))
    {
        if (JS_NewClass(JS_GetRuntime(cx), QJS_CORE_CLASS_ID_WEBCRYPTO_KEY,
                        &qjs_webcrypto_key_class))
        {
            return NULL;
        }

        proto = JS_NewObject(cx);
        if (JS_IsException(proto)) {
            return NULL;
        }

        JS_SetPropertyFunctionList(cx, proto, qjs_webcrypto_key_proto,
                                   njs_nitems(qjs_webcrypto_key_proto));

        JS_SetClassProto(cx, QJS_CORE_CLASS_ID_WEBCRYPTO_KEY, proto);
    }

    global_obj = JS_GetGlobalObject(cx);

    crypto = JS_NewObject(cx);
    JS_SetPropertyFunctionList(cx, crypto, qjs_webcrypto_export,
                               njs_nitems(qjs_webcrypto_export));

    rc = JS_SetPropertyStr(cx, global_obj, "crypto", crypto);
    if (rc == -1) {
        return NULL;
    }

    JS_FreeValue(cx, global_obj);

    m = JS_NewCModule(cx, name, qjs_webcrypto_module_init);
    if (m == NULL) {
        return NULL;
    }

    JS_AddModuleExport(cx, m, "default");
    rc = JS_AddModuleExportList(cx, m, qjs_webcrypto_export,
                                njs_nitems(qjs_webcrypto_export));
    if (rc != 0) {
        return NULL;
    }

    return m;
}
