
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>
#include "njs_webcrypto.h"
#include "njs_openssl.h"

typedef enum {
    NJS_KEY_FORMAT_RAW          = 1 << 1,
    NJS_KEY_FORMAT_PKCS8        = 1 << 2,
    NJS_KEY_FORMAT_SPKI         = 1 << 3,
    NJS_KEY_FORMAT_JWK          = 1 << 4,
    NJS_KEY_FORMAT_UNKNOWN      = 1 << 5,
} njs_webcrypto_key_format_t;


typedef enum {
    NJS_KEY_USAGE_DECRYPT       = 1 << 1,
    NJS_KEY_USAGE_DERIVE_BITS   = 1 << 2,
    NJS_KEY_USAGE_DERIVE_KEY    = 1 << 3,
    NJS_KEY_USAGE_ENCRYPT       = 1 << 4,
    NJS_KEY_USAGE_GENERATE_KEY  = 1 << 5,
    NJS_KEY_USAGE_SIGN          = 1 << 6,
    NJS_KEY_USAGE_VERIFY        = 1 << 7,
    NJS_KEY_USAGE_WRAP_KEY      = 1 << 8,
    NJS_KEY_USAGE_UNSUPPORTED   = 1 << 9,
    NJS_KEY_USAGE_UNWRAP_KEY    = 1 << 10,
} njs_webcrypto_key_usage_t;


typedef enum {
    NJS_ALGORITHM_RSA_OAEP,
    NJS_ALGORITHM_AES_GCM,
    NJS_ALGORITHM_AES_CTR,
    NJS_ALGORITHM_AES_CBC,
    NJS_ALGORITHM_RSASSA_PKCS1_v1_5,
    NJS_ALGORITHM_RSA_PSS,
    NJS_ALGORITHM_ECDSA,
    NJS_ALGORITHM_ECDH,
    NJS_ALGORITHM_PBKDF2,
    NJS_ALGORITHM_HKDF,
    NJS_ALGORITHM_HMAC,
} njs_webcrypto_alg_t;


typedef enum {
    NJS_HASH_SHA1,
    NJS_HASH_SHA256,
    NJS_HASH_SHA384,
    NJS_HASH_SHA512,
} njs_webcrypto_hash_t;


typedef enum {
    NJS_CURVE_P256,
    NJS_CURVE_P384,
    NJS_CURVE_P521,
} njs_webcrypto_curve_t;


typedef struct {
    njs_str_t                  name;
    uintptr_t                  value;
} njs_webcrypto_entry_t;


typedef struct {
    njs_webcrypto_alg_t        type;
    unsigned                   usage;
    unsigned                   fmt;
} njs_webcrypto_algorithm_t;


typedef struct {
    njs_webcrypto_algorithm_t  *alg;
    unsigned                   usage;
    njs_webcrypto_hash_t       hash;
    njs_webcrypto_curve_t      curve;

    EVP_PKEY                   *pkey;
    njs_str_t                  raw;
} njs_webcrypto_key_t;


typedef int (*EVP_PKEY_cipher_init_t)(EVP_PKEY_CTX *ctx);
typedef int (*EVP_PKEY_cipher_t)(EVP_PKEY_CTX *ctx, unsigned char *out,
    size_t *outlen, const unsigned char *in, size_t inlen);


static njs_int_t njs_ext_cipher(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_cipher_pkey(njs_vm_t *vm, njs_str_t *data,
    njs_webcrypto_key_t *key, njs_index_t encrypt);
static njs_int_t njs_cipher_aes_gcm(njs_vm_t *vm, njs_str_t *data,
    njs_webcrypto_key_t *key, njs_value_t *options, njs_bool_t encrypt);
static njs_int_t njs_cipher_aes_ctr(njs_vm_t *vm, njs_str_t *data,
    njs_webcrypto_key_t *key, njs_value_t *options, njs_bool_t encrypt);
static njs_int_t njs_cipher_aes_cbc(njs_vm_t *vm, njs_str_t *data,
    njs_webcrypto_key_t *key, njs_value_t *options, njs_bool_t encrypt);
static njs_int_t njs_ext_derive(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t derive_key);
static njs_int_t njs_ext_digest(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_export_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_generate_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_import_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_sign(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t verify);
static njs_int_t njs_ext_unwrap_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_wrap_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);
static njs_int_t njs_ext_get_random_values(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused);

static void njs_webcrypto_cleanup_pkey(void *data);
static njs_webcrypto_key_format_t njs_key_format(njs_vm_t *vm,
    njs_value_t *value, njs_str_t *format);
static njs_int_t njs_key_usage(njs_vm_t *vm, njs_value_t *value,
    unsigned *mask);
static njs_webcrypto_algorithm_t *njs_key_algorithm(njs_vm_t *vm,
    njs_value_t *value);
static njs_str_t *njs_algorithm_string(njs_webcrypto_algorithm_t *algorithm);
static njs_int_t njs_algorithm_hash(njs_vm_t *vm, njs_value_t *value,
    njs_webcrypto_hash_t *hash);
static const EVP_MD *njs_algorithm_hash_digest(njs_webcrypto_hash_t hash);
static njs_int_t njs_algorithm_curve(njs_vm_t *vm, njs_value_t *value,
    njs_webcrypto_curve_t *curve);

static njs_int_t njs_webcrypto_result(njs_vm_t *vm, njs_value_t *result,
    njs_int_t rc);
static void njs_webcrypto_error(njs_vm_t *vm, const char *fmt, ...);

static njs_webcrypto_entry_t njs_webcrypto_alg[] = {

#define njs_webcrypto_algorithm(type, usage_mask, fmt_mask)                  \
    (uintptr_t) & (njs_webcrypto_algorithm_t) { type, usage_mask, fmt_mask }

    {
      njs_str("RSA-OAEP"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_RSA_OAEP,
                              NJS_KEY_USAGE_ENCRYPT |
                              NJS_KEY_USAGE_DECRYPT |
                              NJS_KEY_USAGE_WRAP_KEY |
                              NJS_KEY_USAGE_UNWRAP_KEY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_PKCS8 |
                              NJS_KEY_FORMAT_SPKI)
    },

    {
      njs_str("AES-GCM"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_AES_GCM,
                              NJS_KEY_USAGE_ENCRYPT |
                              NJS_KEY_USAGE_DECRYPT |
                              NJS_KEY_USAGE_WRAP_KEY |
                              NJS_KEY_USAGE_UNWRAP_KEY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_RAW)
    },

    {
      njs_str("AES-CTR"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_AES_CTR,
                              NJS_KEY_USAGE_ENCRYPT |
                              NJS_KEY_USAGE_DECRYPT |
                              NJS_KEY_USAGE_WRAP_KEY |
                              NJS_KEY_USAGE_UNWRAP_KEY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_RAW)
    },

    {
      njs_str("AES-CBC"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_AES_CBC,
                              NJS_KEY_USAGE_ENCRYPT |
                              NJS_KEY_USAGE_DECRYPT |
                              NJS_KEY_USAGE_WRAP_KEY |
                              NJS_KEY_USAGE_UNWRAP_KEY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_RAW)
    },

    {
      njs_str("RSASSA-PKCS1-v1_5"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_RSASSA_PKCS1_v1_5,
                              NJS_KEY_USAGE_SIGN |
                              NJS_KEY_USAGE_VERIFY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_PKCS8 |
                              NJS_KEY_FORMAT_SPKI)
    },

    {
      njs_str("RSA-PSS"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_RSA_PSS,
                              NJS_KEY_USAGE_SIGN |
                              NJS_KEY_USAGE_VERIFY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_PKCS8 |
                              NJS_KEY_FORMAT_SPKI)
    },

    {
      njs_str("ECDSA"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_ECDSA,
                              NJS_KEY_USAGE_SIGN |
                              NJS_KEY_USAGE_VERIFY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_PKCS8 |
                              NJS_KEY_FORMAT_SPKI)
    },

    {
      njs_str("ECDH"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_ECDH,
                              NJS_KEY_USAGE_DERIVE_KEY |
                              NJS_KEY_USAGE_DERIVE_BITS |
                              NJS_KEY_USAGE_GENERATE_KEY |
                              NJS_KEY_USAGE_UNSUPPORTED,
                              NJS_KEY_FORMAT_UNKNOWN)
    },

    {
      njs_str("PBKDF2"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_PBKDF2,
                              NJS_KEY_USAGE_DERIVE_KEY |
                              NJS_KEY_USAGE_DERIVE_BITS,
                              NJS_KEY_FORMAT_RAW)
    },

    {
      njs_str("HKDF"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_HKDF,
                              NJS_KEY_USAGE_DERIVE_KEY |
                              NJS_KEY_USAGE_DERIVE_BITS,
                              NJS_KEY_FORMAT_RAW)
    },

    {
      njs_str("HMAC"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_HMAC,
                              NJS_KEY_USAGE_GENERATE_KEY |
                              NJS_KEY_USAGE_SIGN |
                              NJS_KEY_USAGE_VERIFY,
                              NJS_KEY_FORMAT_RAW)
    },

    {
        njs_null_str,
        0
    }
};


static njs_webcrypto_entry_t njs_webcrypto_hash[] = {
    { njs_str("SHA-256"), NJS_HASH_SHA256 },
    { njs_str("SHA-384"), NJS_HASH_SHA384 },
    { njs_str("SHA-512"), NJS_HASH_SHA512 },
    { njs_str("SHA-1"), NJS_HASH_SHA1 },
    { njs_null_str, 0 }
};


static njs_webcrypto_entry_t njs_webcrypto_curve[] = {
    { njs_str("P-256"), NJS_CURVE_P256 },
    { njs_str("P-384"), NJS_CURVE_P384 },
    { njs_str("P-521"), NJS_CURVE_P521 },
    { njs_null_str, 0 }
};


static njs_webcrypto_entry_t njs_webcrypto_usage[] = {
    { njs_str("decrypt"), NJS_KEY_USAGE_DECRYPT },
    { njs_str("deriveBits"), NJS_KEY_USAGE_DERIVE_BITS },
    { njs_str("deriveKey"), NJS_KEY_USAGE_DERIVE_KEY },
    { njs_str("encrypt"), NJS_KEY_USAGE_ENCRYPT },
    { njs_str("sign"), NJS_KEY_USAGE_SIGN },
    { njs_str("unwrapKey"), NJS_KEY_USAGE_UNWRAP_KEY },
    { njs_str("verify"), NJS_KEY_USAGE_VERIFY },
    { njs_str("wrapKey"), NJS_KEY_USAGE_WRAP_KEY },
    { njs_null_str, 0 }
};


static njs_external_t  njs_ext_webcrypto_crypto_key[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "CryptoKey",
        }
    },
};


static njs_external_t  njs_ext_subtle_webcrypto[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "SubtleCrypto",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("decrypt"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_cipher,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("deriveBits"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_derive,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("deriveKey"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_derive,
            .magic8 = 1,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("digest"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_digest,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("encrypt"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_cipher,
            .magic8 = 1,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("exportKey"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_export_key,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("generateKey"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_generate_key,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("importKey"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_import_key,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("sign"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_sign,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("unwrapKey"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_unwrap_key,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("verify"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_sign,
            .magic8 = 1,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("wrapKey"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_wrap_key,
        }
    },

};

static njs_external_t  njs_ext_webcrypto[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "Crypto",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("getRandomValues"),
        .writable = 1,
        .configurable = 1,
        .enumerable = 1,
        .u.method = {
            .native = njs_ext_get_random_values,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("subtle"),
        .enumerable = 1,
        .writable = 1,
        .u.object = {
            .enumerable = 1,
            .properties = njs_ext_subtle_webcrypto,
            .nproperties = njs_nitems(njs_ext_subtle_webcrypto),
        }
    },

};


static njs_int_t    njs_webcrypto_crypto_key_proto_id;


static njs_int_t
njs_ext_cipher(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t encrypt)
{
    unsigned                   mask;
    njs_int_t                  ret;
    njs_str_t                  data;
    njs_value_t                *options;
    njs_webcrypto_key_t        *key;
    njs_webcrypto_algorithm_t  *alg;

    options = njs_arg(args, nargs, 1);
    alg = njs_key_algorithm(vm, options);
    if (njs_slow_path(alg == NULL)) {
        goto fail;
    }

    key = njs_vm_external(vm, njs_webcrypto_crypto_key_proto_id,
                          njs_arg(args, nargs, 2));
    if (njs_slow_path(key == NULL)) {
        njs_type_error(vm, "\"key\" is not a CryptoKey object");
        goto fail;
    }

    mask = encrypt ? NJS_KEY_USAGE_ENCRYPT : NJS_KEY_USAGE_DECRYPT;
    if (njs_slow_path(!(key->usage & mask))) {
        njs_type_error(vm, "provide key does not support %s operation",
                       encrypt ? "encrypt" : "decrypt");
        goto fail;
    }

    if (njs_slow_path(key->alg != alg)) {
        njs_type_error(vm, "cannot %s using \"%V\" with \"%V\" key",
                       encrypt ? "encrypt" : "decrypt",
                       njs_algorithm_string(key->alg),
                       njs_algorithm_string(alg));
        goto fail;
    }

    ret = njs_vm_value_to_bytes(vm, &data, njs_arg(args, nargs, 3));
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    switch (alg->type) {
    case NJS_ALGORITHM_RSA_OAEP:
        ret = njs_cipher_pkey(vm, &data, key, encrypt);
        break;

    case NJS_ALGORITHM_AES_GCM:
        ret = njs_cipher_aes_gcm(vm, &data, key, options, encrypt);
        break;

    case NJS_ALGORITHM_AES_CTR:
        ret = njs_cipher_aes_ctr(vm, &data, key, options, encrypt);
        break;

    case NJS_ALGORITHM_AES_CBC:
    default:
        ret = njs_cipher_aes_cbc(vm, &data, key, options, encrypt);
    }

    return njs_webcrypto_result(vm, njs_vm_retval(vm), ret);

fail:

    return njs_webcrypto_result(vm, njs_vm_retval(vm), NJS_ERROR);
}


static njs_int_t
njs_cipher_pkey(njs_vm_t *vm, njs_str_t *data, njs_webcrypto_key_t *key,
    njs_index_t encrypt)
{
    u_char                  *dst;
    size_t                  outlen;
    njs_int_t               ret;
    const EVP_MD            *md;
    EVP_PKEY_CTX            *ctx;
    EVP_PKEY_cipher_t       cipher;
    EVP_PKEY_cipher_init_t  init;

    ctx = EVP_PKEY_CTX_new(key->pkey, NULL);
    if (njs_slow_path(ctx == NULL)) {
        njs_webcrypto_error(vm, "EVP_PKEY_CTX_new() failed");
        return NJS_ERROR;
    }

    if (encrypt) {
        init = EVP_PKEY_encrypt_init;
        cipher = EVP_PKEY_encrypt;

    } else {
        init = EVP_PKEY_decrypt_init;
        cipher = EVP_PKEY_decrypt;
    }

    ret = init(ctx);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_PKEY_%scrypt_init() failed",
                            encrypt ? "en" : "de");
        ret = NJS_ERROR;
        goto fail;
    }

    md = njs_algorithm_hash_digest(key->hash);

    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING);
    EVP_PKEY_CTX_set_signature_md(ctx, md);
    EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, md);

    ret = cipher(ctx, NULL, &outlen, data->start, data->length);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_PKEY_%scrypt() failed",
                            encrypt ? "en" : "de");
        ret = NJS_ERROR;
        goto fail;
    }

    dst = njs_mp_alloc(njs_vm_memory_pool(vm), outlen);
    if (njs_slow_path(dst == NULL)) {
        njs_memory_error(vm);
        ret = NJS_ERROR;
        goto fail;
    }

    ret = cipher(ctx, dst, &outlen, data->start, data->length);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_PKEY_%scrypt() failed",
                            encrypt ? "en" : "de");
        ret = NJS_ERROR;
        goto fail;
    }

    ret = njs_vm_value_array_buffer_set(vm, njs_vm_retval(vm), dst, outlen);

fail:

    EVP_PKEY_CTX_free(ctx);

    return ret;
}


static njs_int_t
njs_cipher_aes_gcm(njs_vm_t *vm, njs_str_t *data, njs_webcrypto_key_t *key,
    njs_value_t *options, njs_bool_t encrypt)
{
    int               len, outlen, dstlen;
    u_char            *dst, *p;
    int64_t           taglen;
    njs_str_t         iv, aad;
    njs_int_t         ret;
    njs_value_t       value;
    EVP_CIPHER_CTX    *ctx;
    const EVP_CIPHER  *cipher;

    static const njs_value_t  string_iv = njs_string("iv");
    static const njs_value_t  string_ad = njs_string("additionalData");
    static const njs_value_t  string_tl = njs_string("tagLength");

    switch (key->raw.length) {
    case 16:
        cipher = EVP_aes_128_gcm();
        break;

    case 32:
        cipher = EVP_aes_256_gcm();
        break;

    default:
        njs_type_error(vm, "AES-GCM Invalid key length");
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, options, njs_value_arg(&string_iv), &value);
    if (njs_slow_path(ret != NJS_OK)) {
        if (ret == NJS_DECLINED) {
            njs_type_error(vm, "AES-GCM algorithm.iv is not provided");
        }

        return NJS_ERROR;
    }

    ret = njs_vm_value_to_bytes(vm, &iv, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    taglen = 128;

    ret = njs_value_property(vm, options, njs_value_arg(&string_tl), &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    if (njs_is_defined(&value)) {
        ret = njs_value_to_integer(vm, &value, &taglen);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (njs_slow_path(taglen != 32
                      && taglen != 64
                      && taglen != 96
                      && taglen != 104
                      && taglen != 112
                      && taglen != 120
                      && taglen != 128))
    {
        njs_type_error(vm, "AES-GCM Invalid tagLength");
        return NJS_ERROR;
    }

    taglen /= 8;

    if (njs_slow_path(!encrypt && (data->length < (size_t) taglen))) {
        njs_type_error(vm, "AES-GCM data is too short");
        return NJS_ERROR;
    }

    ctx = EVP_CIPHER_CTX_new();
    if (njs_slow_path(ctx == NULL)) {
        njs_webcrypto_error(vm, "EVP_CIPHER_CTX_new() failed");
        return NJS_ERROR;
    }

    ret = EVP_CipherInit_ex(ctx, cipher, NULL, NULL, NULL, encrypt);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%sInit_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.length, NULL);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_CIPHER_CTX_ctrl() failed");
        ret = NJS_ERROR;
        goto fail;
    }

    ret = EVP_CipherInit_ex(ctx, NULL, NULL, key->raw.start, iv.start,
                            encrypt);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%sInit_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    if (!encrypt) {
        ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, taglen,
                                  &data->start[data->length - taglen]);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_CIPHER_CTX_ctrl() failed");
            ret = NJS_ERROR;
            goto fail;
        }
    }

    ret = njs_value_property(vm, options, njs_value_arg(&string_ad), &value);
    if (njs_slow_path(ret == NJS_ERROR)) {
        return NJS_ERROR;
    }

    aad.length = 0;

    if (njs_is_defined(&value)) {
        ret = njs_vm_value_to_bytes(vm, &aad, &value);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    if (aad.length != 0) {
        ret = EVP_CipherUpdate(ctx, NULL, &outlen, aad.start, aad.length);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_%sUpdate() failed",
                                encrypt ? "Encrypt" : "Decrypt");
            ret = NJS_ERROR;
            goto fail;
        }
    }

    dstlen = data->length + EVP_CIPHER_CTX_block_size(ctx) + taglen;
    dst = njs_mp_alloc(njs_vm_memory_pool(vm), dstlen);
    if (njs_slow_path(dst == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    ret = EVP_CipherUpdate(ctx, dst, &outlen, data->start,
                           data->length - (encrypt ? 0 : taglen));
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%sUpdate() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    p = &dst[outlen];
    len = EVP_CIPHER_CTX_block_size(ctx);

    ret = EVP_CipherFinal_ex(ctx, p, &len);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%sFinal_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    outlen += len;
    p += len;

    if (encrypt) {
        ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, taglen, p);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_CIPHER_CTX_ctrl() failed");
            ret = NJS_ERROR;
            goto fail;
        }

        outlen += taglen;
    }

    ret = njs_vm_value_array_buffer_set(vm, njs_vm_retval(vm), dst, outlen);

fail:

    EVP_CIPHER_CTX_free(ctx);

    return ret;
}


static njs_int_t
njs_cipher_aes_ctr128(njs_vm_t *vm, const EVP_CIPHER *cipher, u_char *key,
    u_char *data, size_t dlen, u_char *counter, u_char *dst, int *olen,
    njs_bool_t encrypt)
{
    int             len, outlen;
    njs_int_t       ret;
    EVP_CIPHER_CTX  *ctx;

    ctx = EVP_CIPHER_CTX_new();
    if (njs_slow_path(ctx == NULL)) {
        njs_webcrypto_error(vm, "EVP_CIPHER_CTX_new() failed");
        return NJS_ERROR;
    }

    ret = EVP_CipherInit_ex(ctx, cipher, NULL, key, counter, encrypt);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%sInit_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    ret = EVP_CipherUpdate(ctx, dst, &outlen, data, dlen);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%sUpdate() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    ret = EVP_CipherFinal_ex(ctx, &dst[outlen], &len);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%sFinal_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    outlen += len;
    *olen = outlen;

    ret = NJS_OK;

fail:

    EVP_CIPHER_CTX_free(ctx);

    return ret;
}


njs_inline njs_uint_t
njs_ceil_div(njs_uint_t dend, njs_uint_t dsor)
{
    return (dsor == 0) ? 0 : 1 + (dend - 1) / dsor;
}


njs_inline BIGNUM *
njs_bn_counter128(njs_str_t *ctr, njs_uint_t bits)
{
    njs_uint_t  remainder, bytes;
    uint8_t     buf[16];

    remainder = bits % 8;

    if (remainder == 0) {
        bytes = bits / 8;

        return BN_bin2bn(&ctr->start[ctr->length - bytes], bytes, NULL);
    }

    bytes = njs_ceil_div(bits, 8);

    memcpy(buf, &ctr->start[ctr->length - bytes], bytes);

    buf[0] &= ~(0xFF << remainder);

    return BN_bin2bn(buf, bytes, NULL);
}


njs_inline void
njs_counter128_reset(u_char *src, u_char *dst, njs_uint_t bits)
{
    size_t      index;
    njs_uint_t  remainder, bytes;

    bytes = bits / 8;
    remainder = bits % 8;

    memcpy(dst, src, 16);

    index = 16 - bytes;

    memset(&dst[index], 0, bytes);

    if (remainder) {
        dst[index - 1] &= 0xff << remainder;
    }
}


static njs_int_t
njs_cipher_aes_ctr(njs_vm_t *vm, njs_str_t *data, njs_webcrypto_key_t *key,
    njs_value_t *options, njs_bool_t encrypt)
{
    int               len, len2;
    u_char            *dst;
    int64_t           length;
    BIGNUM            *total, *blocks, *left, *ctr;
    njs_int_t         ret;
    njs_str_t         iv;
    njs_uint_t        size1;
    njs_value_t       value;
    const EVP_CIPHER  *cipher;
    u_char            iv2[16];

    static const njs_value_t  string_counter = njs_string("counter");
    static const njs_value_t  string_length = njs_string("length");

    switch (key->raw.length) {
    case 16:
        cipher = EVP_aes_128_ctr();
        break;

    case 32:
        cipher = EVP_aes_256_ctr();
        break;

    default:
        njs_type_error(vm, "AES-CTR Invalid key length");
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, options, njs_value_arg(&string_counter),
                             &value);
    if (njs_slow_path(ret != NJS_OK)) {
        if (ret == NJS_DECLINED) {
            njs_type_error(vm, "AES-CTR algorithm.counter is not provided");
        }

        return NJS_ERROR;
    }

    ret = njs_vm_value_to_bytes(vm, &iv, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(iv.length != 16)) {
        njs_type_error(vm, "AES-CTR algorithm.counter must be 16 bytes long");
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, options, njs_value_arg(&string_length),
                             &value);
    if (njs_slow_path(ret != NJS_OK)) {
        if (ret == NJS_DECLINED) {
            njs_type_error(vm, "AES-CTR algorithm.length is not provided");
        }

        return NJS_ERROR;
    }

    ret = njs_value_to_integer(vm, &value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(length == 0 || length > 128)) {
        njs_type_error(vm, "AES-CTR algorithm.length "
                       "must be between 1 and 128");
        return NJS_ERROR;
    }

    ctr = NULL;
    blocks = NULL;
    left = NULL;

    total = BN_new();
    if (njs_slow_path(total == NULL)) {
        njs_webcrypto_error(vm, "BN_new() failed");
        return NJS_ERROR;
    }

    ret = BN_lshift(total, BN_value_one(), length);
    if (njs_slow_path(ret != 1)) {
        njs_webcrypto_error(vm, "BN_lshift() failed");
        ret = NJS_ERROR;
        goto fail;
    }

    ctr = njs_bn_counter128(&iv, length);
    if (njs_slow_path(ctr == NULL)) {
        njs_webcrypto_error(vm, "BN_bin2bn() failed");
        ret = NJS_ERROR;
        goto fail;
    }

    blocks = BN_new();
    if (njs_slow_path(blocks == NULL)) {
        njs_webcrypto_error(vm, "BN_new() failed");
        return NJS_ERROR;
    }

    ret = BN_set_word(blocks, njs_ceil_div(data->length, AES_BLOCK_SIZE));
    if (njs_slow_path(ret != 1)) {
        njs_webcrypto_error(vm, "BN_set_word() failed");
        ret = NJS_ERROR;
        goto fail;
    }

    ret = BN_cmp(blocks, total);
    if (njs_slow_path(ret > 0)) {
        njs_type_error(vm, "AES-CTR repeated counter");
        ret = NJS_ERROR;
        goto fail;
    }

    left = BN_new();
    if (njs_slow_path(left == NULL)) {
        njs_webcrypto_error(vm, "BN_new() failed");
        return NJS_ERROR;
    }

    ret = BN_sub(left, total, ctr);
    if (njs_slow_path(ret != 1)) {
        njs_webcrypto_error(vm, "BN_sub() failed");
        ret = NJS_ERROR;
        goto fail;
    }

    dst = njs_mp_alloc(njs_vm_memory_pool(vm),
                       data->length + EVP_MAX_BLOCK_LENGTH);
    if (njs_slow_path(dst == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    ret = BN_cmp(left, blocks);
    if (ret >= 0) {

        /*
         * Doing a single run if a counter is not wrapped-around
         * during the ciphering.
         * */

        ret = njs_cipher_aes_ctr128(vm, cipher, key->raw.start,
                                    data->start, data->length, iv.start, dst,
                                    &len, encrypt);
        if (njs_slow_path(ret != NJS_OK)) {
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

    ret = njs_cipher_aes_ctr128(vm, cipher, key->raw.start, data->start, size1,
                                iv.start, dst, &len, encrypt);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    njs_counter128_reset(iv.start, (u_char *) iv2, length);

    ret = njs_cipher_aes_ctr128(vm, cipher, key->raw.start, &data->start[size1],
                                data->length - size1, iv2, &dst[size1], &len2,
                                encrypt);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    len += len2;

done:

    ret = njs_vm_value_array_buffer_set(vm, njs_vm_retval(vm), dst, len);

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


static njs_int_t
njs_cipher_aes_cbc(njs_vm_t *vm, njs_str_t *data, njs_webcrypto_key_t *key,
    njs_value_t *options, njs_bool_t encrypt)
{
    int               olen_max, olen, olen2;
    u_char            *dst;
    unsigned          remainder;
    njs_str_t         iv;
    njs_int_t         ret;
    njs_value_t       value;
    EVP_CIPHER_CTX    *ctx;
    const EVP_CIPHER  *cipher;

    static const njs_value_t  string_iv = njs_string("iv");

    switch (key->raw.length) {
    case 16:
        cipher = EVP_aes_128_cbc();
        break;

    case 32:
        cipher = EVP_aes_256_cbc();
        break;

    default:
        njs_type_error(vm, "AES-CBC Invalid key length");
        return NJS_ERROR;
    }

    ret = njs_value_property(vm, options, njs_value_arg(&string_iv), &value);
    if (njs_slow_path(ret != NJS_OK)) {
        if (ret == NJS_DECLINED) {
            njs_type_error(vm, "AES-CBC algorithm.iv is not provided");
        }

        return NJS_ERROR;
    }

    ret = njs_vm_value_to_bytes(vm, &iv, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(iv.length != 16)) {
        njs_type_error(vm, "AES-CBC algorithm.iv must be 16 bytes long");
        return NJS_ERROR;
    }

    olen_max = data->length + AES_BLOCK_SIZE - 1;
    remainder = olen_max % AES_BLOCK_SIZE;

    if (remainder != 0) {
        olen_max += AES_BLOCK_SIZE - remainder;
    }

    ctx = EVP_CIPHER_CTX_new();
    if (njs_slow_path(ctx == NULL)) {
        njs_webcrypto_error(vm, "EVP_CIPHER_CTX_new() failed");
        return NJS_ERROR;
    }

    ret = EVP_CipherInit_ex(ctx, cipher, NULL, key->raw.start, iv.start,
                            encrypt);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%SInit_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    dst = njs_mp_alloc(njs_vm_memory_pool(vm), olen_max);
    if (njs_slow_path(dst == NULL)) {
        njs_memory_error(vm);
        ret = NJS_ERROR;
        goto fail;
    }

    ret = EVP_CipherUpdate(ctx, dst, &olen, data->start, data->length);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%SUpdate() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    ret = EVP_CipherFinal_ex(ctx, &dst[olen], &olen2);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%sFinal_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    olen += olen2;

    ret = njs_vm_value_array_buffer_set(vm, njs_vm_retval(vm), dst, olen);

fail:

    EVP_CIPHER_CTX_free(ctx);

    return ret;
}


static njs_int_t
njs_ext_derive(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t derive_key)
{
    u_char                     *k;
    size_t                     olen;
    int64_t                    iterations, length;
    EVP_PKEY                   *pkey;
    unsigned                   usage, mask;
    njs_int_t                  ret;
    njs_str_t                  salt, info;
    njs_value_t                value, *aobject, *dobject;
    const EVP_MD               *md;
    EVP_PKEY_CTX               *pctx;
    njs_mp_cleanup_t           *cln;
    njs_webcrypto_key_t        *key, *dkey;
    njs_webcrypto_hash_t       hash;
    njs_webcrypto_algorithm_t  *alg, *dalg;

    static const njs_value_t  string_info = njs_string("info");
    static const njs_value_t  string_salt = njs_string("salt");
    static const njs_value_t  string_length = njs_string("length");
    static const njs_value_t  string_iterations = njs_string("iterations");

    aobject = njs_arg(args, nargs, 1);
    alg = njs_key_algorithm(vm, aobject);
    if (njs_slow_path(alg == NULL)) {
        goto fail;
    }

    key = njs_vm_external(vm, njs_webcrypto_crypto_key_proto_id,
                          njs_arg(args, nargs, 2));
    if (njs_slow_path(key == NULL)) {
        njs_type_error(vm, "\"baseKey\" is not a CryptoKey object");
        goto fail;
    }

    mask = derive_key ? NJS_KEY_USAGE_DERIVE_KEY : NJS_KEY_USAGE_DERIVE_BITS;
    if (njs_slow_path(!(key->usage & mask))) {
        njs_type_error(vm, "provide key does not support \"%s\" operation",
                       derive_key ? "deriveKey" : "deriveBits");
        goto fail;
    }

    if (njs_slow_path(key->alg != alg)) {
        njs_type_error(vm, "cannot derive %s using \"%V\" with \"%V\" key",
                       derive_key ? "key" : "bits",
                       njs_algorithm_string(key->alg),
                       njs_algorithm_string(alg));
        goto fail;
    }

    dobject = njs_arg(args, nargs, 3);

    if (derive_key) {
        dalg = njs_key_algorithm(vm, dobject);
        if (njs_slow_path(dalg == NULL)) {
            goto fail;
        }

        ret = njs_value_property(vm, dobject, njs_value_arg(&string_length),
                                 &value);
        if (njs_slow_path(ret != NJS_OK)) {
                if (ret == NJS_DECLINED) {
                    njs_type_error(vm, "derivedKeyAlgorithm.length "
                                   "is not provided");
                    goto fail;
                }
        }

    } else {
        dalg = NULL;
        njs_value_assign(&value, dobject);
    }

    ret = njs_value_to_integer(vm, &value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    dkey = NULL;
    length /= 8;

    if (derive_key) {
        switch (dalg->type) {
        case NJS_ALGORITHM_AES_GCM:
        case NJS_ALGORITHM_AES_CTR:
        case NJS_ALGORITHM_AES_CBC:

            if (length != 16 && length != 32) {
                njs_type_error(vm, "deriveKey \"%V\" length must be 128 or 256",
                               njs_algorithm_string(dalg));
                goto fail;
            }

            break;

        default:
            njs_internal_error(vm, "not implemented deriveKey: \"%V\"",
                               njs_algorithm_string(dalg));
            goto fail;
        }

        ret = njs_key_usage(vm, njs_arg(args, nargs, 5), &usage);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        if (njs_slow_path(usage & ~dalg->usage)) {
            njs_type_error(vm, "unsupported key usage for \"%V\" key",
                           njs_algorithm_string(alg));
            goto fail;
        }

        dkey = njs_mp_zalloc(njs_vm_memory_pool(vm),
                             sizeof(njs_webcrypto_key_t));
        if (njs_slow_path(dkey == NULL)) {
            njs_memory_error(vm);
            goto fail;
        }

        dkey->alg = dalg;
        dkey->usage = usage;
    }

    k = njs_mp_zalloc(njs_vm_memory_pool(vm), length);
    if (njs_slow_path(k == NULL)) {
        njs_memory_error(vm);
        goto fail;
    }

    switch (alg->type) {
    case NJS_ALGORITHM_PBKDF2:
        ret = njs_algorithm_hash(vm, aobject, &hash);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        ret = njs_value_property(vm, aobject, njs_value_arg(&string_salt),
                                 &value);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                njs_type_error(vm, "PBKDF2 algorithm.salt is not provided");
            }

            goto fail;
        }

        ret = njs_vm_value_to_bytes(vm, &salt, &value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        if (njs_slow_path(salt.length < 16)) {
            njs_type_error(vm, "PBKDF2 algorithm.salt must be "
                           "at least 16 bytes long");
            goto fail;
        }

        ret = njs_value_property(vm, aobject, njs_value_arg(&string_iterations),
                                 &value);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                njs_type_error(vm, "PBKDF2 algorithm.iterations "
                               "is not provided");
            }

            goto fail;
        }

        ret = njs_value_to_integer(vm, &value, &iterations);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        md = njs_algorithm_hash_digest(hash);

        ret = PKCS5_PBKDF2_HMAC((char *) key->raw.start, key->raw.length,
                                salt.start, salt.length, iterations, md,
                                length, k);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "PKCS5_PBKDF2_HMAC() failed");
            goto fail;
        }
        break;

    case NJS_ALGORITHM_HKDF:
#ifdef EVP_PKEY_HKDF
        ret = njs_algorithm_hash(vm, aobject, &hash);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        ret = njs_value_property(vm, aobject, njs_value_arg(&string_salt),
                                 &value);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                njs_type_error(vm, "HKDF algorithm.salt is not provided");
            }

            goto fail;
        }

        ret = njs_vm_value_to_bytes(vm, &salt, &value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        ret = njs_value_property(vm, aobject, njs_value_arg(&string_info),
                                 &value);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                njs_type_error(vm, "HKDF algorithm.info is not provided");
            }

            goto fail;
        }

        ret = njs_vm_value_to_bytes(vm, &info, &value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
        if (njs_slow_path(pctx == NULL)) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_new_id() failed");
            goto fail;
        }

        ret = EVP_PKEY_derive_init(pctx);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_PKEY_derive_init() failed");
            goto free;
        }

        md = njs_algorithm_hash_digest(hash);

        ret = EVP_PKEY_CTX_set_hkdf_md(pctx, md);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_set_hkdf_md() failed");
            goto free;
        }

        ret = EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt.start, salt.length);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_set1_hkdf_salt() failed");
            goto free;
        }

        ret = EVP_PKEY_CTX_set1_hkdf_key(pctx, key->raw.start, key->raw.length);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_set1_hkdf_key() failed");
            goto free;
        }

        ret = EVP_PKEY_CTX_add1_hkdf_info(pctx, info.start, info.length);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_add1_hkdf_info() failed");
            goto free;
        }

        olen = (size_t) length;
        ret = EVP_PKEY_derive(pctx, k, &olen);
        if (njs_slow_path(ret <= 0 || olen != (size_t) length)) {
            njs_webcrypto_error(vm, "EVP_PKEY_derive() failed");
            goto free;
        }

free:

        EVP_PKEY_CTX_free(pctx);

        if (njs_slow_path(ret <= 0)) {
            goto fail;
        }

        break;
#else
        (void) pctx;
        (void) olen;
        (void) &string_info;
        (void) &info;
#endif

    case NJS_ALGORITHM_ECDH:
    default:
        njs_internal_error(vm, "not implemented deriveKey "
                           "algorithm: \"%V\"", njs_algorithm_string(alg));
        goto fail;
    }

    if (derive_key) {
        if (dalg->type == NJS_ALGORITHM_HMAC) {
            ret = njs_algorithm_hash(vm, dobject, &dkey->hash);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto fail;
            }

            pkey = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, NULL, k, length);
            if (njs_slow_path(pkey == NULL)) {
                njs_webcrypto_error(vm, "EVP_PKEY_new_mac_key() failed");
                goto fail;
            }

            cln = njs_mp_cleanup_add(njs_vm_memory_pool(vm), 0);
            if (cln == NULL) {
                njs_memory_error(vm);
                goto fail;
            }

            cln->handler = njs_webcrypto_cleanup_pkey;
            cln->data = key;

            dkey->pkey = pkey;

        } else {
            dkey->raw.start = k;
            dkey->raw.length = length;
        }

        ret = njs_vm_external_create(vm, &value,
                                     njs_webcrypto_crypto_key_proto_id,
                                     dkey, 0);
    } else {
        ret = njs_vm_value_array_buffer_set(vm, &value, k, length);
    }

    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    return njs_webcrypto_result(vm, &value, NJS_OK);

fail:

    return njs_webcrypto_result(vm, njs_vm_retval(vm), NJS_ERROR);
}


static njs_int_t
njs_ext_digest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    unsigned              olen;
    u_char                *dst;
    njs_str_t             data;
    njs_int_t             ret;
    njs_value_t           value;
    const EVP_MD          *md;
    njs_webcrypto_hash_t  hash;

    ret = njs_algorithm_hash(vm, njs_arg(args, nargs, 1), &hash);
    if (njs_slow_path(ret == NJS_ERROR)) {
        goto fail;
    }

    ret = njs_vm_value_to_bytes(vm, &data, njs_arg(args, nargs, 2));
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    md = njs_algorithm_hash_digest(hash);
    olen = EVP_MD_size(md);

    dst = njs_mp_zalloc(njs_vm_memory_pool(vm), olen);
    if (njs_slow_path(dst == NULL)) {
        njs_memory_error(vm);
        goto fail;
    }

    ret = EVP_Digest(data.start, data.length, dst, &olen, md, NULL);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_Digest() failed");
        goto fail;
    }

    ret = njs_vm_value_array_buffer_set(vm, &value, dst, olen);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    return njs_webcrypto_result(vm, &value, NJS_OK);

fail:

    return njs_webcrypto_result(vm, njs_vm_retval(vm), NJS_ERROR);
}


static njs_int_t
njs_ext_export_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_internal_error(vm, "\"exportKey\" not implemented");
    return NJS_ERROR;
}


static njs_int_t
njs_ext_generate_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_internal_error(vm, "\"generateKey\" not implemented");
    return NJS_ERROR;
}


static njs_int_t
njs_ext_import_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    int                         nid;
    BIO                         *bio;
#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
    RSA                         *rsa;
    EC_KEY                      *ec;
#else
    char                        gname[80];
#endif
    unsigned                    usage;
    EVP_PKEY                    *pkey;
    njs_int_t                   ret;
    njs_str_t                   key_data, format;
    njs_value_t                 value, *options;
    const u_char                *start;
#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
    const EC_GROUP              *group;
#endif
    njs_mp_cleanup_t            *cln;
    njs_webcrypto_key_t         *key;
    PKCS8_PRIV_KEY_INFO         *pkcs8;
    njs_webcrypto_algorithm_t   *alg;
    njs_webcrypto_key_format_t  fmt;

    static const int curves[] = {
        NID_X9_62_prime256v1,
        NID_secp384r1,
        NID_secp521r1,
    };

    pkey = NULL;

    fmt = njs_key_format(vm, njs_arg(args, nargs, 1), &format);
    if (njs_slow_path(fmt == NJS_KEY_FORMAT_UNKNOWN)) {
        njs_type_error(vm, "unknown key format: \"%V\"", &format);
        goto fail;
    }

    options = njs_arg(args, nargs, 3);
    alg = njs_key_algorithm(vm, options);
    if (njs_slow_path(alg == NULL)) {
        goto fail;
    }

    if (njs_slow_path(!(fmt & alg->fmt))) {
        njs_type_error(vm, "unsupported key fmt for \"%V\" key",
                       njs_algorithm_string(alg));
        goto fail;
    }

    ret = njs_key_usage(vm, njs_arg(args, nargs, 5), &usage);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    if (njs_slow_path(usage & ~alg->usage)) {
        njs_type_error(vm, "unsupported key usage for \"%V\" key",
                       njs_algorithm_string(alg));
        goto fail;
    }

    ret = njs_vm_value_to_bytes(vm, &key_data, njs_arg(args, nargs, 2));
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    start = key_data.start;

    switch (fmt) {
    case NJS_KEY_FORMAT_PKCS8:
        bio = njs_bio_new_mem_buf(start, key_data.length);
        if (njs_slow_path(bio == NULL)) {
            njs_webcrypto_error(vm, "BIO_new_mem_buf() failed");
            goto fail;
        }

        pkcs8 = d2i_PKCS8_PRIV_KEY_INFO_bio(bio, NULL);
        if (njs_slow_path(pkcs8 == NULL)) {
            BIO_free(bio);
            njs_webcrypto_error(vm, "d2i_PKCS8_PRIV_KEY_INFO_bio() failed");
            goto fail;
        }

        pkey = EVP_PKCS82PKEY(pkcs8);
        if (njs_slow_path(pkey == NULL)) {
            PKCS8_PRIV_KEY_INFO_free(pkcs8);
            BIO_free(bio);
            njs_webcrypto_error(vm, "EVP_PKCS82PKEY() failed");
            goto fail;
        }

        PKCS8_PRIV_KEY_INFO_free(pkcs8);
        BIO_free(bio);

        break;

    case NJS_KEY_FORMAT_SPKI:
        pkey = d2i_PUBKEY(NULL, &start, key_data.length);
        if (njs_slow_path(pkey == NULL)) {
            njs_webcrypto_error(vm, "d2i_PUBKEY() failed");
            goto fail;
        }

        break;

    case NJS_KEY_FORMAT_RAW:
        break;

    default:
        njs_internal_error(vm, "not implemented key format: \"%V\"", &format);
        goto fail;
    }

    key = njs_mp_zalloc(njs_vm_memory_pool(vm), sizeof(njs_webcrypto_key_t));
    if (njs_slow_path(key == NULL)) {
        njs_memory_error(vm);
        goto fail;
    }

    key->alg = alg;
    key->usage = usage;

    switch (alg->type) {
    case NJS_ALGORITHM_RSA_OAEP:
    case NJS_ALGORITHM_RSA_PSS:
    case NJS_ALGORITHM_RSASSA_PKCS1_v1_5:

#if (OPENSSL_VERSION_NUMBER < 0x30000000L)

        rsa = EVP_PKEY_get1_RSA(pkey);
        if (njs_slow_path(rsa == NULL)) {
            njs_webcrypto_error(vm, "RSA key is not found");
            goto fail;
        }

        RSA_free(rsa);

#else
        if (!EVP_PKEY_is_a(pkey, "RSA")) {
            njs_webcrypto_error(vm, "RSA key is not found");
            goto fail;
        }
#endif

        ret = njs_algorithm_hash(vm, options, &key->hash);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        key->pkey = pkey;

        break;

    case NJS_ALGORITHM_ECDSA:
    case NJS_ALGORITHM_ECDH:

#if (OPENSSL_VERSION_NUMBER < 0x30000000L)

        ec = EVP_PKEY_get1_EC_KEY(pkey);
        if (njs_slow_path(ec == NULL)) {
            njs_webcrypto_error(vm, "EC key is not found");
            goto fail;
        }

        group = EC_KEY_get0_group(ec);
        nid = EC_GROUP_get_curve_name(group);
        EC_KEY_free(ec);

#else

        if (!EVP_PKEY_is_a(pkey, "EC")) {
            njs_webcrypto_error(vm, "EC key is not found");
            goto fail;
        }

        if (EVP_PKEY_get_group_name(pkey, gname, sizeof(gname), NULL) != 1) {
            njs_webcrypto_error(vm, "EVP_PKEY_get_group_name() failed");
            goto fail;
        }

        nid = OBJ_txt2nid(gname);

#endif

        ret = njs_algorithm_curve(vm, options, &key->curve);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        if (njs_slow_path(curves[key->curve] != nid)) {
            njs_webcrypto_error(vm, "name curve mismatch");
            goto fail;
        }

        key->pkey = pkey;

        break;

    case NJS_ALGORITHM_HMAC:
        pkey = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, NULL, key_data.start,
                                    key_data.length);
        if (njs_slow_path(pkey == NULL)) {
            njs_webcrypto_error(vm, "EVP_PKEY_new_mac_key() failed");
            goto fail;
        }

        ret = njs_algorithm_hash(vm, options, &key->hash);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        key->pkey = pkey;

        break;

    case NJS_ALGORITHM_AES_GCM:
    case NJS_ALGORITHM_AES_CTR:
    case NJS_ALGORITHM_AES_CBC:
    case NJS_ALGORITHM_PBKDF2:
    case NJS_ALGORITHM_HKDF:
        key->raw = key_data;
    default:
        break;
    }

    if (pkey != NULL) {
        cln = njs_mp_cleanup_add(njs_vm_memory_pool(vm), 0);
        if (cln == NULL) {
            njs_memory_error(vm);
            goto fail;
        }

        cln->handler = njs_webcrypto_cleanup_pkey;
        cln->data = key;
        pkey = NULL;
    }

    ret = njs_vm_external_create(vm, &value, njs_webcrypto_crypto_key_proto_id,
                                 key, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    return njs_webcrypto_result(vm, &value, NJS_OK);

fail:

    if (pkey != NULL) {
        EVP_PKEY_free(pkey);
    }

    return njs_webcrypto_result(vm, njs_vm_retval(vm), NJS_ERROR);
}


static njs_int_t
njs_set_rsa_padding(njs_vm_t *vm, njs_value_t *options, EVP_PKEY *pkey,
    EVP_PKEY_CTX *ctx, njs_webcrypto_alg_t type)
{
    int          padding;
    int64_t      salt_length;
    njs_int_t    ret;
    njs_value_t  value;

    static const njs_value_t  string_saltl = njs_string("saltLength");

    if (type == NJS_ALGORITHM_ECDSA) {
        return NJS_OK;
    }

    padding = (type == NJS_ALGORITHM_RSA_PSS) ? RSA_PKCS1_PSS_PADDING
                                              : RSA_PKCS1_PADDING;
    ret = EVP_PKEY_CTX_set_rsa_padding(ctx, padding);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_PKEY_CTX_set_rsa_padding() failed");
        return NJS_ERROR;
    }

    if (padding == RSA_PKCS1_PSS_PADDING) {
        ret = njs_value_property(vm, options, njs_value_arg(&string_saltl),
                                 &value);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                njs_type_error(vm, "RSA-PSS algorithm.saltLength "
                               "is not provided");
            }

            return NJS_ERROR;
        }

        ret = njs_value_to_integer(vm, &value, &salt_length);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx, salt_length);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm,
                                "EVP_PKEY_CTX_set_rsa_pss_saltlen() failed");
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_ext_sign(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t verify)
{
    u_char                     *dst;
    size_t                     olen, outlen;
    unsigned                   mask, m_len;
    njs_int_t                  ret;
    njs_str_t                  data, sig;
    EVP_MD_CTX                 *mctx;
    njs_value_t                value, *options;
    EVP_PKEY_CTX               *pctx;
    const EVP_MD               *md;
    njs_webcrypto_key_t        *key;
    njs_webcrypto_hash_t       hash;
    njs_webcrypto_algorithm_t  *alg;
    unsigned char              m[EVP_MAX_MD_SIZE];

    mctx = NULL;
    pctx = NULL;

    options = njs_arg(args, nargs, 1);
    alg = njs_key_algorithm(vm, options);
    if (njs_slow_path(alg == NULL)) {
        goto fail;
    }

    key = njs_vm_external(vm, njs_webcrypto_crypto_key_proto_id,
                          njs_arg(args, nargs, 2));
    if (njs_slow_path(key == NULL)) {
        njs_type_error(vm, "\"key\" is not a CryptoKey object");
        goto fail;
    }

    mask = verify ? NJS_KEY_USAGE_VERIFY : NJS_KEY_USAGE_SIGN;
    if (njs_slow_path(!(key->usage & mask))) {
        njs_type_error(vm, "provide key does not support \"sign\" operation");
        goto fail;
    }

    if (njs_slow_path(key->alg != alg)) {
        njs_type_error(vm, "cannot %s using \"%V\" with \"%V\" key",
                       verify ? "verify" : "sign",
                       njs_algorithm_string(key->alg),
                       njs_algorithm_string(alg));
        goto fail;
    }

    if (verify) {
        ret = njs_vm_value_to_bytes(vm, &sig, njs_arg(args, nargs, 3));
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        ret = njs_vm_value_to_bytes(vm, &data, njs_arg(args, nargs, 4));
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

    } else {
        ret = njs_vm_value_to_bytes(vm, &data, njs_arg(args, nargs, 3));
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }
    }

    mctx = njs_evp_md_ctx_new();
    if (njs_slow_path(mctx == NULL)) {
        njs_webcrypto_error(vm, "njs_evp_md_ctx_new() failed");
        goto fail;
    }

    if (alg->type == NJS_ALGORITHM_ECDSA) {
        ret = njs_algorithm_hash(vm, options, &hash);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

    } else {
        hash = key->hash;
    }

    md = njs_algorithm_hash_digest(hash);

    outlen = 0;

    switch (alg->type) {
    case NJS_ALGORITHM_HMAC:
        ret = EVP_DigestSignInit(mctx, NULL, md, NULL, key->pkey);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_DigestSignInit() failed");
            goto fail;
        }

        ret = EVP_DigestSignUpdate(mctx, data.start, data.length);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_DigestSignUpdate() failed");
            goto fail;
        }

        olen = EVP_MD_size(md);

        if (!verify) {
            dst = njs_mp_zalloc(njs_vm_memory_pool(vm), olen);
            if (njs_slow_path(dst == NULL)) {
                njs_memory_error(vm);
                goto fail;
            }

        } else {
            dst = (u_char *) &m[0];
        }

        ret = EVP_DigestSignFinal(mctx, dst, &outlen);
        if (njs_slow_path(ret <= 0 || olen != outlen)) {
            njs_webcrypto_error(vm, "EVP_DigestSignFinal() failed");
            goto fail;
        }

        if (verify) {
            ret = (sig.length == outlen && memcmp(sig.start, dst, outlen) == 0);
        }

        break;

    case NJS_ALGORITHM_RSASSA_PKCS1_v1_5:
    case NJS_ALGORITHM_RSA_PSS:
    case NJS_ALGORITHM_ECDSA:
    default:
        ret = EVP_DigestInit_ex(mctx, md, NULL);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_DigestInit_ex() failed");
            goto fail;
        }

        ret = EVP_DigestUpdate(mctx, data.start, data.length);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_DigestUpdate() failed");
            goto fail;
        }

        ret = EVP_DigestFinal_ex(mctx, m, &m_len);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_DigestFinal_ex() failed");
            goto fail;
        }

        olen = EVP_PKEY_size(key->pkey);
        dst = njs_mp_zalloc(njs_vm_memory_pool(vm), olen);
        if (njs_slow_path(dst == NULL)) {
            njs_memory_error(vm);
            goto fail;
        }

        pctx = EVP_PKEY_CTX_new(key->pkey, NULL);
        if (njs_slow_path(pctx == NULL)) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_new() failed");
            goto fail;
        }

        if (!verify) {
            ret = EVP_PKEY_sign_init(pctx);
            if (njs_slow_path(ret <= 0)) {
                njs_webcrypto_error(vm, "EVP_PKEY_sign_init() failed");
                goto fail;
            }

        } else {
            ret = EVP_PKEY_verify_init(pctx);
            if (njs_slow_path(ret <= 0)) {
                njs_webcrypto_error(vm, "EVP_PKEY_verify_init() failed");
                goto fail;
            }
        }

        ret = njs_set_rsa_padding(vm, options, key->pkey, pctx, alg->type);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        ret = EVP_PKEY_CTX_set_signature_md(pctx, md);
        if (njs_slow_path(ret <= 0)) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_set_signature_md() failed");
            goto fail;
        }

        if (!verify) {
            outlen = olen;
            ret = EVP_PKEY_sign(pctx, dst, &outlen, m, m_len);
            if (njs_slow_path(ret <= 0)) {
                njs_webcrypto_error(vm, "EVP_PKEY_sign() failed");
                goto fail;
            }

        } else {
            ret = EVP_PKEY_verify(pctx, sig.start, sig.length, m, m_len);
            if (njs_slow_path(ret < 0)) {
                njs_webcrypto_error(vm, "EVP_PKEY_verify() failed");
                goto fail;
            }
        }

        EVP_PKEY_CTX_free(pctx);

        break;
    }

    if (!verify) {
        ret = njs_vm_value_array_buffer_set(vm, &value, dst, outlen);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

    } else {
        njs_set_boolean(&value, ret != 0);
    }

    njs_evp_md_ctx_free(mctx);

    return njs_webcrypto_result(vm, &value, NJS_OK);

fail:

    if (mctx != NULL) {
        njs_evp_md_ctx_free(mctx);
    }

    if (pctx != NULL) {
        EVP_PKEY_CTX_free(pctx);
    }

    return njs_webcrypto_result(vm, njs_vm_retval(vm), NJS_ERROR);
}


static njs_int_t
njs_ext_unwrap_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_internal_error(vm, "\"unwrapKey\" not implemented");
    return NJS_ERROR;
}


static njs_int_t
njs_ext_wrap_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_internal_error(vm, "\"wrapKey\" not implemented");
    return NJS_ERROR;
}


static njs_int_t
njs_ext_get_random_values(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_int_t  ret;
    njs_str_t  fill;

    ret = njs_vm_value_to_bytes(vm, &fill, njs_arg(args, nargs, 1));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(fill.length > 65536)) {
        njs_type_error(vm, "requested length exceeds 65536 bytes");
        return NJS_ERROR;
    }

    if (RAND_bytes(fill.start, fill.length) != 1) {
        njs_webcrypto_error(vm, "RAND_bytes() failed");
        return NJS_ERROR;
    }

    return NJS_OK;
}


static void
njs_webcrypto_cleanup_pkey(void *data)
{
    njs_webcrypto_key_t  *key = data;

    if (key->pkey != NULL) {
        EVP_PKEY_free(key->pkey);
    }
}


static njs_webcrypto_key_format_t
njs_key_format(njs_vm_t *vm, njs_value_t *value, njs_str_t *format)
{
    njs_int_t   ret;
    njs_uint_t  fmt;

    static const struct {
        njs_str_t   name;
        njs_uint_t  value;
    } formats[] = {
        { njs_str("raw"), NJS_KEY_FORMAT_RAW },
        { njs_str("pkcs8"), NJS_KEY_FORMAT_PKCS8 },
        { njs_str("spki"), NJS_KEY_FORMAT_SPKI },
        { njs_str("jwk"), NJS_KEY_FORMAT_JWK },
    };

    ret = njs_value_to_string(vm, value, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_string_get(value, format);

    fmt = 0;

    while (fmt < sizeof(formats) / sizeof(formats[0])) {
        if (njs_strstr_eq(format, &formats[fmt].name)) {
            return formats[fmt].value;
        }

        fmt++;
    }

    return NJS_KEY_FORMAT_UNKNOWN;
}


static njs_int_t
njs_key_usage_array_handler(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *value, int64_t index)
{
    unsigned               *mask;
    njs_str_t              u;
    njs_int_t              ret;
    njs_value_t            usage;
    njs_webcrypto_entry_t  *e;

    njs_value_assign(&usage, value);

    ret = njs_value_to_string(vm, &usage, &usage);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_string_get(&usage, &u);

    for (e = &njs_webcrypto_usage[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&u, &e->name)) {
            mask = args->data;
            *mask |= e->value;
            return NJS_OK;
        }
    }

    njs_type_error(vm, "unknown key usage: \"%V\"", &u);

    return NJS_ERROR;
}


static njs_int_t
njs_key_usage(njs_vm_t *vm, njs_value_t *value, unsigned *mask)
{
    int64_t              length;
    njs_int_t            ret;
    njs_iterator_args_t  args;

    ret = njs_object_length(vm, value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    *mask = 0;

    args.value = value;
    args.from = 0;
    args.to = length;
    args.data = mask;

    return njs_object_iterate(vm, &args, njs_key_usage_array_handler);
}


static njs_webcrypto_algorithm_t *
njs_key_algorithm(njs_vm_t *vm, njs_value_t *options)
{
    njs_int_t                  ret;
    njs_str_t                  a;
    njs_value_t                name;
    njs_webcrypto_entry_t      *e;
    njs_webcrypto_algorithm_t  *alg;

    static const njs_value_t  string_name = njs_string("name");

    if (njs_is_object(options)) {
        ret = njs_value_property(vm, options, njs_value_arg(&string_name),
                                 &name);
        if (njs_slow_path(ret != NJS_OK)) {
            if (ret == NJS_DECLINED) {
                njs_type_error(vm, "algorithm name is not provided");
            }

            return NULL;
        }

    } else {
        njs_value_assign(&name, options);
    }

    ret = njs_value_to_string(vm, &name, &name);
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    njs_string_get(&name, &a);

    for (e = &njs_webcrypto_alg[0]; e->name.length != 0; e++) {
        if (njs_strstr_case_eq(&a, &e->name)) {
            alg = (njs_webcrypto_algorithm_t *) e->value;
            if (alg->usage & NJS_KEY_USAGE_UNSUPPORTED) {
                njs_type_error(vm, "unsupported algorithm: \"%V\"", &a);
                return NULL;
            }

            return alg;
        }
    }

    njs_type_error(vm, "unknown algorithm name: \"%V\"", &a);

    return NULL;
}


static njs_str_t *
njs_algorithm_string(njs_webcrypto_algorithm_t *algorithm)
{
    njs_webcrypto_entry_t      *e;
    njs_webcrypto_algorithm_t  *alg;

    for (e = &njs_webcrypto_alg[0]; e->name.length != 0; e++) {
        alg = (njs_webcrypto_algorithm_t *) e->value;
        if (alg->type == algorithm->type) {
            break;
        }
    }

    return &e->name;
}


static njs_int_t
njs_algorithm_hash(njs_vm_t *vm, njs_value_t *options,
    njs_webcrypto_hash_t *hash)
{
    njs_int_t              ret;
    njs_str_t              name;
    njs_value_t            value;
    njs_webcrypto_entry_t  *e;

    static const njs_value_t  string_hash = njs_string("hash");

    if (njs_is_object(options)) {
        ret = njs_value_property(vm, options, njs_value_arg(&string_hash),
                                 &value);
        if (njs_slow_path(ret == NJS_ERROR)) {
            return NJS_ERROR;
        }

    } else {
        njs_value_assign(&value, options);
    }

    ret = njs_value_to_string(vm, &value, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_string_get(&value, &name);

    for (e = &njs_webcrypto_hash[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&name, &e->name)) {
            *hash = e->value;
            return NJS_OK;
        }
    }

    njs_type_error(vm, "unknown hash name: \"%V\"", &name);

    return NJS_ERROR;
}


static const EVP_MD *
njs_algorithm_hash_digest(njs_webcrypto_hash_t hash)
{
    switch (hash) {
    case NJS_HASH_SHA256:
        return EVP_sha256();

    case NJS_HASH_SHA384:
        return EVP_sha384();

    case NJS_HASH_SHA512:
        return EVP_sha512();

    case NJS_HASH_SHA1:
    default:
        break;
    }

    return EVP_sha1();
}


static njs_int_t
njs_algorithm_curve(njs_vm_t *vm, njs_value_t *options,
    njs_webcrypto_curve_t *curve)
{
    njs_int_t              ret;
    njs_str_t              name;
    njs_value_t            value;
    njs_webcrypto_entry_t  *e;

    static const njs_value_t  string_curve = njs_string("namedCurve");

    ret = njs_value_property(vm, options, njs_value_arg(&string_curve),
                             &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    ret = njs_value_to_string(vm, &value, &value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_string_get(&value, &name);

    for (e = &njs_webcrypto_curve[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&name, &e->name)) {
            *curve = e->value;
            return NJS_OK;
        }
    }

    njs_type_error(vm, "unknown namedCurve: \"%V\"", &name);

    return NJS_ERROR;
}


static njs_int_t
njs_promise_trampoline(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    njs_function_t  *callback;

    callback = njs_value_function(njs_argument(args, 1));

    if (callback != NULL) {
        return njs_vm_call(vm, callback, njs_argument(args, 2), 1);
    }

    return NJS_OK;
}


static njs_int_t
njs_webcrypto_result(njs_vm_t *vm, njs_value_t *result, njs_int_t rc)
{
    njs_int_t       ret;
    njs_value_t     retval, arguments[2];
    njs_function_t  *callback;
    njs_vm_event_t  vm_event;

    ret = njs_vm_promise_create(vm, &retval, njs_value_arg(&arguments));
    if (ret != NJS_OK) {
        goto error;
    }

    callback = njs_vm_function_alloc(vm, njs_promise_trampoline);
    if (callback == NULL) {
        goto error;
    }

    vm_event = njs_vm_add_event(vm, callback, 1, NULL, NULL);
    if (vm_event == NULL) {
        goto error;
    }

    njs_value_assign(&arguments[0], &arguments[(rc != NJS_OK)]);
    njs_value_assign(&arguments[1], result);

    ret = njs_vm_post_event(vm, vm_event, njs_value_arg(&arguments), 2);
    if (ret == NJS_ERROR) {
        goto error;
    }

    njs_vm_retval_set(vm, njs_value_arg(&retval));

    return NJS_OK;

error:

    njs_vm_error(vm, "internal error");

    return NJS_ERROR;
}


static u_char *
njs_cpystrn(u_char *dst, u_char *src, size_t n)
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


static void
njs_webcrypto_error(njs_vm_t *vm, const char *fmt, ...)
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
        p = njs_cpystrn(p, (u_char *) " (SSL:", last - p);

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
                p = njs_cpystrn(p, (u_char *) data, last - p);
            }

        next:

            (void) ERR_get_error();
        }

        if (p < last) {
            *p++ = ')';
        }
    }

    njs_vm_value_error_set(vm, njs_vm_retval(vm), "%*s", p - errstr, errstr);
}


njs_int_t
njs_external_webcrypto_init(njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_str_t           name;
    njs_opaque_value_t  value;

#if (OPENSSL_VERSION_NUMBER < 0x10100003L)
    OpenSSL_add_all_algorithms();
#endif

    njs_webcrypto_crypto_key_proto_id =
        njs_vm_external_prototype(vm, njs_ext_webcrypto_crypto_key,
                                  njs_nitems(njs_ext_webcrypto_crypto_key));
    if (njs_slow_path(njs_webcrypto_crypto_key_proto_id < 0)) {
        return NJS_ERROR;
    }

    proto_id = njs_vm_external_prototype(vm, njs_ext_webcrypto,
                                         njs_nitems(njs_ext_webcrypto));
    if (njs_slow_path(proto_id < 0)) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    name.length = njs_length("crypto");
    name.start = (u_char *) "crypto";

    ret = njs_vm_bind(vm, &name, njs_value_arg(&value), 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}
