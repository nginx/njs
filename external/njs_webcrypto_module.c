
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs.h>
#include <njs_assert.h>
#include <njs_string.h>
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
    NJS_ALGORITHM_RSASSA_PKCS1_v1_5 = 0,
    NJS_ALGORITHM_RSA_PSS,
    NJS_ALGORITHM_RSA_OAEP,
    NJS_ALGORITHM_HMAC,
    NJS_ALGORITHM_AES_GCM,
    NJS_ALGORITHM_AES_CTR,
    NJS_ALGORITHM_AES_CBC,
    NJS_ALGORITHM_ECDSA,
    NJS_ALGORITHM_ECDH,
    NJS_ALGORITHM_PBKDF2,
    NJS_ALGORITHM_HKDF,
    NJS_ALGORITHM_MAX,
} njs_webcrypto_alg_t;


typedef enum {
    NJS_HASH_UNSET = 0,
    NJS_HASH_SHA1,
    NJS_HASH_SHA256,
    NJS_HASH_SHA384,
    NJS_HASH_SHA512,
    NJS_HASH_MAX,
} njs_webcrypto_hash_t;


typedef struct {
    njs_str_t                  name;
    uintptr_t                  value;
} njs_webcrypto_entry_t;


typedef struct {
    njs_webcrypto_alg_t        type;
    unsigned                   usage;
    unsigned                   fmt;
    unsigned                   raw;
} njs_webcrypto_algorithm_t;


typedef struct {
    njs_webcrypto_algorithm_t  *alg;
    unsigned                   usage;
    njs_bool_t                 extractable;

    njs_webcrypto_hash_t       hash;

    union {
        struct {
            EVP_PKEY          *pkey;
            njs_bool_t        privat;
            int               curve;
        } a;
        struct {
            njs_str_t         raw;
        } s;
    } u;

} njs_webcrypto_key_t;


typedef int (*EVP_PKEY_cipher_init_t)(EVP_PKEY_CTX *ctx);
typedef int (*EVP_PKEY_cipher_t)(EVP_PKEY_CTX *ctx, unsigned char *out,
    size_t *outlen, const unsigned char *in, size_t inlen);


static njs_int_t njs_ext_cipher(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_cipher_pkey(njs_vm_t *vm, njs_str_t *data,
    njs_webcrypto_key_t *key, njs_index_t encrypt, njs_value_t *retval);
static njs_int_t njs_cipher_aes_gcm(njs_vm_t *vm, njs_str_t *data,
    njs_webcrypto_key_t *key, njs_value_t *options, njs_bool_t encrypt,
    njs_value_t *retval);
static njs_int_t njs_cipher_aes_ctr(njs_vm_t *vm, njs_str_t *data,
    njs_webcrypto_key_t *key, njs_value_t *options, njs_bool_t encrypt,
    njs_value_t *retval);
static njs_int_t njs_cipher_aes_cbc(njs_vm_t *vm, njs_str_t *data,
    njs_webcrypto_key_t *key, njs_value_t *options, njs_bool_t encrypt,
    njs_value_t *retval);
static njs_int_t njs_ext_derive(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t derive_key, njs_value_t *retval);
static njs_int_t njs_ext_digest(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_ext_export_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_ext_generate_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_ext_import_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_ext_sign(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t verify, njs_value_t *retval);
static njs_int_t njs_ext_unwrap_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_ext_wrap_key(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_key_ext_algorithm(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_key_ext_extractable(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_key_ext_type(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_key_ext_usages(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_ext_get_random_values(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);

static njs_webcrypto_key_t *njs_webcrypto_key_alloc(njs_vm_t *vm,
    njs_webcrypto_algorithm_t *alg, unsigned usage, njs_bool_t extractable);
static njs_webcrypto_key_format_t njs_key_format(njs_vm_t *vm,
    njs_value_t *value);
static njs_str_t *njs_format_string(njs_webcrypto_key_format_t fmt);
static njs_int_t njs_key_usage(njs_vm_t *vm, njs_value_t *value,
    unsigned *mask);
static njs_int_t njs_key_ops(njs_vm_t *vm, njs_value_t *retval, unsigned mask);
static njs_webcrypto_algorithm_t *njs_key_algorithm(njs_vm_t *vm,
    njs_value_t *value);
static njs_str_t *njs_algorithm_string(njs_webcrypto_algorithm_t *algorithm);
static njs_int_t njs_algorithm_hash(njs_vm_t *vm, njs_value_t *value,
    njs_webcrypto_hash_t *hash);
static njs_str_t *njs_algorithm_hash_name(njs_webcrypto_hash_t hash);
static const EVP_MD *njs_algorithm_hash_digest(njs_webcrypto_hash_t hash);
static njs_int_t njs_algorithm_curve(njs_vm_t *vm, njs_value_t *value,
    int *curve);
static njs_str_t *njs_algorithm_curve_name(int curve);

static njs_int_t njs_webcrypto_result(njs_vm_t *vm, njs_opaque_value_t *result,
    njs_int_t rc, njs_value_t *retval);
static njs_int_t njs_webcrypto_array_buffer(njs_vm_t *vm, njs_value_t *retval,
    u_char *start, size_t length);
static void njs_webcrypto_error(njs_vm_t *vm, const char *fmt, ...);

static njs_int_t njs_webcrypto_init(njs_vm_t *vm);

static njs_webcrypto_entry_t njs_webcrypto_alg[] = {

#define njs_webcrypto_algorithm(type, usage, fmt, raw)                       \
    (uintptr_t) & (njs_webcrypto_algorithm_t) { type, usage, fmt, raw }

    {
      njs_str("RSASSA-PKCS1-v1_5"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_RSASSA_PKCS1_v1_5,
                              NJS_KEY_USAGE_SIGN |
                              NJS_KEY_USAGE_VERIFY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_PKCS8 |
                              NJS_KEY_FORMAT_SPKI |
                              NJS_KEY_FORMAT_JWK,
                              0)
    },

    {
      njs_str("RSA-PSS"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_RSA_PSS,
                              NJS_KEY_USAGE_SIGN |
                              NJS_KEY_USAGE_VERIFY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_PKCS8 |
                              NJS_KEY_FORMAT_SPKI |
                              NJS_KEY_FORMAT_JWK,
                              0)
    },

    {
      njs_str("RSA-OAEP"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_RSA_OAEP,
                              NJS_KEY_USAGE_ENCRYPT |
                              NJS_KEY_USAGE_DECRYPT |
                              NJS_KEY_USAGE_WRAP_KEY |
                              NJS_KEY_USAGE_UNWRAP_KEY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_PKCS8 |
                              NJS_KEY_FORMAT_SPKI |
                              NJS_KEY_FORMAT_JWK,
                              0)
    },

    {
      njs_str("HMAC"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_HMAC,
                              NJS_KEY_USAGE_GENERATE_KEY |
                              NJS_KEY_USAGE_SIGN |
                              NJS_KEY_USAGE_VERIFY,
                              NJS_KEY_FORMAT_RAW |
                              NJS_KEY_FORMAT_JWK,
                              1)
    },

    {
      njs_str("AES-GCM"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_AES_GCM,
                              NJS_KEY_USAGE_ENCRYPT |
                              NJS_KEY_USAGE_DECRYPT |
                              NJS_KEY_USAGE_WRAP_KEY |
                              NJS_KEY_USAGE_UNWRAP_KEY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_RAW |
                              NJS_KEY_FORMAT_JWK,
                              1)
    },

    {
      njs_str("AES-CTR"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_AES_CTR,
                              NJS_KEY_USAGE_ENCRYPT |
                              NJS_KEY_USAGE_DECRYPT |
                              NJS_KEY_USAGE_WRAP_KEY |
                              NJS_KEY_USAGE_UNWRAP_KEY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_RAW |
                              NJS_KEY_FORMAT_JWK,
                              1)
    },

    {
      njs_str("AES-CBC"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_AES_CBC,
                              NJS_KEY_USAGE_ENCRYPT |
                              NJS_KEY_USAGE_DECRYPT |
                              NJS_KEY_USAGE_WRAP_KEY |
                              NJS_KEY_USAGE_UNWRAP_KEY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_RAW |
                              NJS_KEY_FORMAT_JWK,
                              1)
    },

    {
      njs_str("ECDSA"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_ECDSA,
                              NJS_KEY_USAGE_SIGN |
                              NJS_KEY_USAGE_VERIFY |
                              NJS_KEY_USAGE_GENERATE_KEY,
                              NJS_KEY_FORMAT_PKCS8 |
                              NJS_KEY_FORMAT_SPKI |
                              NJS_KEY_FORMAT_RAW |
                              NJS_KEY_FORMAT_JWK,
                              0)
    },

    {
      njs_str("ECDH"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_ECDH,
                              NJS_KEY_USAGE_DERIVE_KEY |
                              NJS_KEY_USAGE_DERIVE_BITS |
                              NJS_KEY_USAGE_GENERATE_KEY |
                              NJS_KEY_USAGE_UNSUPPORTED,
                              NJS_KEY_FORMAT_UNKNOWN,
                              0)
    },

    {
      njs_str("PBKDF2"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_PBKDF2,
                              NJS_KEY_USAGE_DERIVE_KEY |
                              NJS_KEY_USAGE_DERIVE_BITS,
                              NJS_KEY_FORMAT_RAW,
                              1)
    },

    {
      njs_str("HKDF"),
      njs_webcrypto_algorithm(NJS_ALGORITHM_HKDF,
                              NJS_KEY_USAGE_DERIVE_KEY |
                              NJS_KEY_USAGE_DERIVE_BITS,
                              NJS_KEY_FORMAT_RAW,
                              1)
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
    { njs_str("P-256"), NID_X9_62_prime256v1 },
    { njs_str("P-384"), NID_secp384r1 },
    { njs_str("P-521"), NID_secp521r1 },
    { njs_null_str, 0 }
};


static njs_webcrypto_entry_t njs_webcrypto_format[] = {
    { njs_str("raw"), NJS_KEY_FORMAT_RAW },
    { njs_str("pkcs8"), NJS_KEY_FORMAT_PKCS8 },
    { njs_str("spki"), NJS_KEY_FORMAT_SPKI },
    { njs_str("jwk"), NJS_KEY_FORMAT_JWK },
    { njs_null_str, NJS_KEY_FORMAT_UNKNOWN }
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


static njs_webcrypto_entry_t njs_webcrypto_alg_hash[] = {
    { njs_str("RS1"), NJS_HASH_SHA1 },
    { njs_str("RS256"), NJS_HASH_SHA256 },
    { njs_str("RS384"), NJS_HASH_SHA384 },
    { njs_str("RS512"), NJS_HASH_SHA512 },
    { njs_str("PS1"), NJS_HASH_SHA1 },
    { njs_str("PS256"), NJS_HASH_SHA256 },
    { njs_str("PS384"), NJS_HASH_SHA384 },
    { njs_str("PS512"), NJS_HASH_SHA512 },
    { njs_str("RSA-OAEP"), NJS_HASH_SHA1 },
    { njs_str("RSA-OAEP-256"), NJS_HASH_SHA256 },
    { njs_str("RSA-OAEP-384"), NJS_HASH_SHA384 },
    { njs_str("RSA-OAEP-512"), NJS_HASH_SHA512 },
    { njs_null_str, 0 }
};


static njs_str_t
    njs_webcrypto_alg_name[NJS_ALGORITHM_HMAC + 1][NJS_HASH_MAX] = {
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

static njs_str_t njs_webcrypto_alg_aes_name[3][3 + 1] = {
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


static njs_external_t  njs_ext_webcrypto_crypto_key[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "CryptoKey",
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("algorithm"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_key_ext_algorithm,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("extractable"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_key_ext_extractable,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("type"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_key_ext_type,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("usages"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_key_ext_usages,
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


njs_module_t  njs_webcrypto_module = {
    .name = njs_str("webcrypto"),
    .preinit = NULL,
    .init = njs_webcrypto_init,
};


static const njs_str_t  string_alg = njs_str("alg");
static const njs_str_t  string_d = njs_str("d");
static const njs_str_t  string_dp = njs_str("dp");
static const njs_str_t  string_dq = njs_str("dq");
static const njs_str_t  string_e = njs_str("e");
static const njs_str_t  string_k = njs_str("k");
static const njs_str_t  string_n = njs_str("n");
static const njs_str_t  string_p = njs_str("p");
static const njs_str_t  string_q = njs_str("q");
static const njs_str_t  string_qi = njs_str("qi");
static const njs_str_t  string_x = njs_str("x");
static const njs_str_t  string_y = njs_str("y");
static const njs_str_t  string_ext = njs_str("ext");
static const njs_str_t  string_crv = njs_str("crv");
static const njs_str_t  string_kty = njs_str("kty");
static const njs_str_t  key_ops = njs_str("key_ops");
static const njs_str_t  string_hash = njs_str("hash");
static const njs_str_t  string_name = njs_str("name");
static const njs_str_t  string_length = njs_str("length");
static const njs_str_t  string_ml = njs_str("modulusLength");
static const njs_str_t  string_curve = njs_str("namedCurve");


static njs_int_t    njs_webcrypto_crypto_key_proto_id;


static njs_int_t
njs_ext_cipher(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t encrypt, njs_value_t *retval)
{
    unsigned                   mask;
    njs_int_t                  ret;
    njs_str_t                  data;
    njs_value_t                *options;
    njs_opaque_value_t         result;
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
        njs_vm_type_error(vm, "\"key\" is not a CryptoKey object");
        goto fail;
    }

    mask = encrypt ? NJS_KEY_USAGE_ENCRYPT : NJS_KEY_USAGE_DECRYPT;
    if (njs_slow_path(!(key->usage & mask))) {
        njs_vm_type_error(vm, "provide key does not support %s operation",
                          encrypt ? "encrypt" : "decrypt");
        goto fail;
    }

    if (njs_slow_path(key->alg != alg)) {
        njs_vm_type_error(vm, "cannot %s using \"%V\" with \"%V\" key",
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
        ret = njs_cipher_pkey(vm, &data, key, encrypt, njs_value_arg(&result));
        break;

    case NJS_ALGORITHM_AES_GCM:
        ret = njs_cipher_aes_gcm(vm, &data, key, options, encrypt,
                                 njs_value_arg(&result));
        break;

    case NJS_ALGORITHM_AES_CTR:
        ret = njs_cipher_aes_ctr(vm, &data, key, options, encrypt,
                                 njs_value_arg(&result));
        break;

    case NJS_ALGORITHM_AES_CBC:
    default:
        ret = njs_cipher_aes_cbc(vm, &data, key, options, encrypt,
                                 njs_value_arg(&result));
    }

    return njs_webcrypto_result(vm, &result, ret, retval);

fail:

    return njs_webcrypto_result(vm, NULL, NJS_ERROR, retval);
}


static njs_int_t
njs_cipher_pkey(njs_vm_t *vm, njs_str_t *data, njs_webcrypto_key_t *key,
    njs_index_t encrypt, njs_value_t *retval)
{
    u_char                  *dst;
    size_t                  outlen;
    njs_int_t               ret;
    const EVP_MD            *md;
    EVP_PKEY_CTX            *ctx;
    EVP_PKEY_cipher_t       cipher;
    EVP_PKEY_cipher_init_t  init;

    ctx = EVP_PKEY_CTX_new(key->u.a.pkey, NULL);
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
        njs_vm_memory_error(vm);
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

    ret = njs_vm_value_array_buffer_set(vm, retval, dst, outlen);

fail:

    EVP_PKEY_CTX_free(ctx);

    return ret;
}


static njs_int_t
njs_cipher_aes_gcm(njs_vm_t *vm, njs_str_t *data, njs_webcrypto_key_t *key,
    njs_value_t *options, njs_bool_t encrypt, njs_value_t *retval)
{
    int                 len, outlen, dstlen;
    u_char              *dst, *p;
    int64_t             taglen;
    njs_str_t           iv, aad;
    njs_int_t           ret;
    njs_value_t         *value;
    EVP_CIPHER_CTX      *ctx;
    const EVP_CIPHER    *cipher;
    njs_opaque_value_t  lvalue;

    static const njs_str_t  string_iv = njs_str("iv");
    static const njs_str_t  string_ad = njs_str("additionalData");
    static const njs_str_t  string_tl = njs_str("tagLength");

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
        njs_vm_type_error(vm, "AES-GCM Invalid key length");
        return NJS_ERROR;
    }

    value = njs_vm_object_prop(vm, options, &string_iv, &lvalue);
    if (value == NULL) {
        njs_vm_type_error(vm, "AES-GCM algorithm.iv is not provided");
        return NJS_ERROR;
    }

    ret = njs_vm_value_to_bytes(vm, &iv, njs_value_arg(&lvalue));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    taglen = 128;

    value = njs_vm_object_prop(vm, options, &string_tl, &lvalue);
    if (value != NULL && !njs_value_is_undefined(value)) {
        ret = njs_value_to_integer(vm, value, &taglen);
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
        njs_vm_type_error(vm, "AES-GCM Invalid tagLength");
        return NJS_ERROR;
    }

    taglen /= 8;

    if (njs_slow_path(!encrypt && (data->length < (size_t) taglen))) {
        njs_vm_type_error(vm, "AES-GCM data is too short");
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

    ret = EVP_CipherInit_ex(ctx, NULL, NULL, key->u.s.raw.start, iv.start,
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

    aad.length = 0;

    value = njs_vm_object_prop(vm, options, &string_ad, &lvalue);
    if (value != NULL && !njs_value_is_undefined(value)) {
        ret = njs_vm_value_to_bytes(vm, &aad, value);
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
        njs_vm_memory_error(vm);
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

    ret = njs_vm_value_array_buffer_set(vm, retval, dst, outlen);

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
    njs_value_t *options, njs_bool_t encrypt, njs_value_t *retval)
{
    int                 len, len2;
    u_char              *dst;
    int64_t             length;
    BIGNUM              *total, *blocks, *left, *ctr;
    njs_int_t           ret;
    njs_str_t           iv;
    njs_uint_t          size1;
    njs_value_t         *value;
    const EVP_CIPHER    *cipher;
    njs_opaque_value_t  lvalue;
    u_char              iv2[16];

    static const njs_str_t  string_counter = njs_str("counter");

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
        njs_vm_type_error(vm, "AES-CTR Invalid key length");
        return NJS_ERROR;
    }

    value = njs_vm_object_prop(vm, options, &string_counter, &lvalue);
    if (value == NULL) {
        njs_vm_type_error(vm, "AES-CTR algorithm.counter is not provided");
        return NJS_ERROR;
    }

    ret = njs_vm_value_to_bytes(vm, &iv, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(iv.length != 16)) {
        njs_vm_type_error(vm, "AES-CTR algorithm.counter must be 16 bytes "
                          "long");
        return NJS_ERROR;
    }

    value = njs_vm_object_prop(vm, options, &string_length, &lvalue);
    if (value == NULL) {
        njs_vm_type_error(vm, "AES-CTR algorithm.length is not provided");
        return NJS_ERROR;
    }

    ret = njs_value_to_integer(vm, value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(length == 0 || length > 128)) {
        njs_vm_type_error(vm, "AES-CTR algorithm.length must be between "
                          "1 and 128");
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
        njs_vm_type_error(vm, "AES-CTR repeated counter");
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
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    ret = BN_cmp(left, blocks);
    if (ret >= 0) {

        /*
         * Doing a single run if a counter is not wrapped-around
         * during the ciphering.
         * */

        ret = njs_cipher_aes_ctr128(vm, cipher, key->u.s.raw.start,
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

    ret = njs_cipher_aes_ctr128(vm, cipher, key->u.s.raw.start, data->start,
                                size1, iv.start, dst, &len, encrypt);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    njs_counter128_reset(iv.start, (u_char *) iv2, length);

    ret = njs_cipher_aes_ctr128(vm, cipher, key->u.s.raw.start,
                                &data->start[size1], data->length - size1,
                                iv2, &dst[size1], &len2, encrypt);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    len += len2;

done:

    ret = njs_vm_value_array_buffer_set(vm, retval, dst, len);

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
    njs_value_t *options, njs_bool_t encrypt, njs_value_t *retval)
{
    int                 olen_max, olen, olen2;
    u_char              *dst;
    unsigned            remainder;
    njs_str_t           iv;
    njs_int_t           ret;
    njs_value_t         *value;
    EVP_CIPHER_CTX      *ctx;
    const EVP_CIPHER    *cipher;
    njs_opaque_value_t  lvalue;

    static const njs_str_t  string_iv = njs_str("iv");

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
        njs_vm_type_error(vm, "AES-CBC Invalid key length");
        return NJS_ERROR;
    }

    value = njs_vm_object_prop(vm, options, &string_iv, &lvalue);
    if (value == NULL) {
        njs_vm_type_error(vm, "AES-CBC algorithm.iv is not provided");
        return NJS_ERROR;
    }

    ret = njs_vm_value_to_bytes(vm, &iv, value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(iv.length != 16)) {
        njs_vm_type_error(vm, "AES-CBC algorithm.iv must be 16 bytes long");
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

    ret = EVP_CipherInit_ex(ctx, cipher, NULL, key->u.s.raw.start, iv.start,
                            encrypt);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_%SInit_ex() failed",
                            encrypt ? "Encrypt" : "Decrypt");
        ret = NJS_ERROR;
        goto fail;
    }

    dst = njs_mp_alloc(njs_vm_memory_pool(vm), olen_max);
    if (njs_slow_path(dst == NULL)) {
        njs_vm_memory_error(vm);
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

    ret = njs_vm_value_array_buffer_set(vm, retval, dst, olen);

fail:

    EVP_CIPHER_CTX_free(ctx);

    return ret;
}


static njs_int_t
njs_ext_derive(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t derive_key, njs_value_t *retval)
{
    u_char                     *k;
    size_t                     olen;
    int64_t                    iterations, length;
    unsigned                   usage, mask;
    njs_int_t                  ret;
    njs_str_t                  salt, info;
    njs_value_t                *value, *aobject, *dobject;
    const EVP_MD               *md;
    EVP_PKEY_CTX               *pctx;
    njs_webcrypto_key_t        *key, *dkey;
    njs_opaque_value_t         lvalue;
    njs_webcrypto_hash_t       hash;
    njs_webcrypto_algorithm_t  *alg, *dalg;

    static const njs_str_t  string_info = njs_str("info");
    static const njs_str_t  string_salt = njs_str("salt");
    static const njs_str_t  string_iterations = njs_str("iterations");

    aobject = njs_arg(args, nargs, 1);
    alg = njs_key_algorithm(vm, aobject);
    if (njs_slow_path(alg == NULL)) {
        goto fail;
    }

    key = njs_vm_external(vm, njs_webcrypto_crypto_key_proto_id,
                          njs_arg(args, nargs, 2));
    if (njs_slow_path(key == NULL)) {
        njs_vm_type_error(vm, "\"baseKey\" is not a CryptoKey object");
        goto fail;
    }

    mask = derive_key ? NJS_KEY_USAGE_DERIVE_KEY : NJS_KEY_USAGE_DERIVE_BITS;
    if (njs_slow_path(!(key->usage & mask))) {
        njs_vm_type_error(vm, "provide key does not support \"%s\" operation",
                          derive_key ? "deriveKey" : "deriveBits");
        goto fail;
    }

    if (njs_slow_path(key->alg != alg)) {
        njs_vm_type_error(vm, "cannot derive %s using \"%V\" with \"%V\" key",
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

        value = njs_vm_object_prop(vm, dobject, &string_length, &lvalue);
        if (value == NULL) {
            njs_vm_type_error(vm, "derivedKeyAlgorithm.length is not provided");
            goto fail;
        }

    } else {
        dalg = NULL;
        value = dobject;
    }

    ret = njs_value_to_integer(vm, value, &length);
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
                njs_vm_type_error(vm, "deriveKey \"%V\" length must be "
                                  "128 or 256", njs_algorithm_string(dalg));
                goto fail;
            }

            break;

        default:
            njs_vm_internal_error(vm, "not implemented deriveKey: \"%V\"",
                                  njs_algorithm_string(dalg));
            goto fail;
        }

        ret = njs_key_usage(vm, njs_arg(args, nargs, 5), &usage);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        if (njs_slow_path(usage & ~dalg->usage)) {
            njs_vm_type_error(vm, "unsupported key usage for \"%V\" key",
                              njs_algorithm_string(alg));
            goto fail;
        }

        dkey = njs_mp_zalloc(njs_vm_memory_pool(vm),
                             sizeof(njs_webcrypto_key_t));
        if (njs_slow_path(dkey == NULL)) {
            njs_vm_memory_error(vm);
            goto fail;
        }

        dkey->alg = dalg;
        dkey->usage = usage;
    }

    k = njs_mp_zalloc(njs_vm_memory_pool(vm), length);
    if (njs_slow_path(k == NULL)) {
        njs_vm_memory_error(vm);
        goto fail;
    }

    switch (alg->type) {
    case NJS_ALGORITHM_PBKDF2:
        ret = njs_algorithm_hash(vm, aobject, &hash);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        value = njs_vm_object_prop(vm, aobject, &string_salt, &lvalue);
        if (value == NULL) {
            njs_vm_type_error(vm, "PBKDF2 algorithm.salt is not provided");
            goto fail;
        }

        ret = njs_vm_value_to_bytes(vm, &salt, value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        if (njs_slow_path(salt.length < 16)) {
            njs_vm_type_error(vm, "PBKDF2 algorithm.salt must be "
                              "at least 16 bytes long");
            goto fail;
        }

        value = njs_vm_object_prop(vm, aobject, &string_iterations, &lvalue);
        if (value == NULL) {
            njs_vm_type_error(vm, "PBKDF2 algorithm.iterations is not "
                              "provided");
            goto fail;
        }

        ret = njs_value_to_integer(vm, value, &iterations);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        md = njs_algorithm_hash_digest(hash);

        ret = PKCS5_PBKDF2_HMAC((char *) key->u.s.raw.start, key->u.s.raw.length,
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

        value = njs_vm_object_prop(vm, aobject, &string_salt, &lvalue);
        if (value == NULL) {
            njs_vm_type_error(vm, "HKDF algorithm.salt is not provided");
            goto fail;
        }

        ret = njs_vm_value_to_bytes(vm, &salt, value);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        value = njs_vm_object_prop(vm, aobject, &string_info, &lvalue);
        if (value == NULL) {
            njs_vm_type_error(vm, "HKDF algorithm.info is not provided");
            goto fail;
        }

        ret = njs_vm_value_to_bytes(vm, &info, value);
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

        ret = EVP_PKEY_CTX_set1_hkdf_key(pctx, key->u.s.raw.start,
                                         key->u.s.raw.length);
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
        njs_vm_internal_error(vm, "not implemented deriveKey "
                              "algorithm: \"%V\"", njs_algorithm_string(alg));
        goto fail;
    }

    if (derive_key) {
        if (dalg->type == NJS_ALGORITHM_HMAC) {
            ret = njs_algorithm_hash(vm, dobject, &dkey->hash);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto fail;
            }
        }

        dkey->u.s.raw.start = k;
        dkey->u.s.raw.length = length;

        ret = njs_vm_external_create(vm, njs_value_arg(&lvalue),
                                     njs_webcrypto_crypto_key_proto_id,
                                     dkey, 0);
    } else {
        ret = njs_vm_value_array_buffer_set(vm, njs_value_arg(&lvalue), k,
                                            length);
    }

    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    return njs_webcrypto_result(vm, &lvalue, NJS_OK, retval);

fail:

    return njs_webcrypto_result(vm, NULL, NJS_ERROR, retval);
}


static njs_int_t
njs_ext_digest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    unsigned              olen;
    u_char                *dst;
    njs_str_t             data;
    njs_int_t             ret;
    const EVP_MD          *md;
    njs_opaque_value_t    result;
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
        njs_vm_memory_error(vm);
        goto fail;
    }

    ret = EVP_Digest(data.start, data.length, dst, &olen, md, NULL);
    if (njs_slow_path(ret <= 0)) {
        njs_webcrypto_error(vm, "EVP_Digest() failed");
        goto fail;
    }

    ret = njs_vm_value_array_buffer_set(vm, njs_value_arg(&result), dst, olen);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    return njs_webcrypto_result(vm, &result, NJS_OK, retval);

fail:

    return njs_webcrypto_result(vm, NULL, NJS_ERROR, retval);
}


static njs_int_t
njs_export_base64url_bignum(njs_vm_t *vm, njs_opaque_value_t *retval,
    const BIGNUM *v, size_t size)
{
    njs_str_t  src;
    u_char     buf[512];

    if (size == 0) {
        size = BN_num_bytes(v);
    }

    if (njs_bn_bn2binpad(v, &buf[0], size) <= 0) {
        return NJS_ERROR;
    }

    src.start = buf;
    src.length = size;

    return njs_string_base64url(vm, njs_value_arg(retval), &src);
}


static njs_int_t
njs_base64url_bignum_set(njs_vm_t *vm, njs_value_t *jwk, const njs_str_t *key,
    const BIGNUM *v, size_t size)
{
    njs_int_t           ret;
    njs_opaque_value_t  value;

    ret = njs_export_base64url_bignum(vm, &value, v, size);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    return njs_vm_object_prop_set(vm, jwk, key, &value);
}


static njs_int_t
njs_export_jwk_rsa(njs_vm_t *vm, njs_webcrypto_key_t *key, njs_value_t *retval)
{
    njs_int_t           ret;
    const RSA           *rsa;
    njs_str_t           *nm;
    const BIGNUM        *n_bn, *e_bn, *d_bn, *p_bn, *q_bn, *dp_bn, *dq_bn,
                        *qi_bn;
    njs_opaque_value_t  nvalue, evalue, alg, rsa_s;

    rsa = njs_pkey_get_rsa_key(key->u.a.pkey);

    njs_rsa_get0_key(rsa, &n_bn, &e_bn, &d_bn);

    ret = njs_export_base64url_bignum(vm, &nvalue, n_bn, 0);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_export_base64url_bignum(vm, &evalue, e_bn, 0);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_alloc(vm, retval, NULL);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    njs_vm_value_string_create(vm, njs_value_arg(&rsa_s), (u_char *) "RSA", 3);

    ret = njs_vm_object_prop_set(vm, retval, &string_kty, &rsa_s);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_prop_set(vm, retval, &string_n, &nvalue);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_prop_set(vm, retval, &string_e, &evalue);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    if (key->u.a.privat) {
        njs_rsa_get0_factors(rsa, &p_bn, &q_bn);
        njs_rsa_get0_ctr_params(rsa, &dp_bn, &dq_bn, &qi_bn);

        ret = njs_base64url_bignum_set(vm, retval, &string_d, d_bn, 0);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        ret = njs_base64url_bignum_set(vm, retval, &string_p, p_bn, 0);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        ret = njs_base64url_bignum_set(vm, retval, &string_q, q_bn, 0);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        ret = njs_base64url_bignum_set(vm, retval, &string_dp, dp_bn, 0);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        ret = njs_base64url_bignum_set(vm, retval, &string_dq, dq_bn, 0);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }

        ret = njs_base64url_bignum_set(vm, retval, &string_qi, qi_bn, 0);
        if (ret != NJS_OK) {
            return NJS_ERROR;
        }
    }

    nm = &njs_webcrypto_alg_name[key->alg->type][key->hash];

    (void) njs_vm_value_string_create(vm, njs_value_arg(&alg), nm->start,
                                      nm->length);

    return njs_vm_object_prop_set(vm, retval, &string_alg, &alg);
}


static njs_int_t
njs_export_jwk_ec(njs_vm_t *vm, njs_webcrypto_key_t *key, njs_value_t *retval)
{
    int                 nid, group_bits, group_bytes;
    BIGNUM              *x_bn, *y_bn;
    njs_int_t           ret;
    njs_str_t           *cname;
    const EC_KEY        *ec;
    const BIGNUM        *d_bn;
    const EC_POINT      *pub;
    const EC_GROUP      *group;
    njs_opaque_value_t  xvalue, yvalue, dvalue, name, ec_s;

    x_bn = NULL;
    y_bn = NULL;
    d_bn = NULL;

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
        njs_webcrypto_error(vm, "EC_POINT_get_affine_coordinates() failed");
        goto fail;
    }

    ret = njs_export_base64url_bignum(vm, &xvalue, x_bn, group_bytes);
    if (ret != NJS_OK) {
        goto fail;
    }

    BN_free(x_bn);
    x_bn = NULL;

    ret = njs_export_base64url_bignum(vm, &yvalue, y_bn, group_bytes);
    if (ret != NJS_OK) {
        goto fail;
    }

    BN_free(y_bn);
    y_bn = NULL;

    nid = EC_GROUP_get_curve_name(group);

    cname = njs_algorithm_curve_name(nid);
    (void) njs_vm_value_string_create(vm, njs_value_arg(&name),
                                      cname->start, cname->length);

    if (cname->length == 0) {
        njs_vm_type_error(vm, "Unsupported JWK EC curve: %s", OBJ_nid2sn(nid));
        goto fail;
    }

    ret = njs_vm_object_alloc(vm, retval, NULL);
    if (ret != NJS_OK) {
        goto fail;
    }

    njs_vm_value_string_create(vm, njs_value_arg(&ec_s), (u_char *) "EC", 2);

    ret = njs_vm_object_prop_set(vm, retval, &string_kty, &ec_s);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_prop_set(vm, retval, &string_x, &xvalue);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_prop_set(vm, retval, &string_y, &yvalue);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_prop_set(vm, retval, &string_crv, &name);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    if (key->u.a.privat) {
        d_bn = EC_KEY_get0_private_key(ec);

        ret = njs_export_base64url_bignum(vm, &dvalue, d_bn, group_bytes);
        if (ret != NJS_OK) {
            goto fail;
        }

        ret = njs_vm_object_prop_set(vm, retval, &string_d, &dvalue);
        if (ret != NJS_OK) {
            goto fail;
        }
    }

    return NJS_OK;

fail:

    if (x_bn != NULL) {
        BN_free(x_bn);
    }

    if (y_bn != NULL) {
        BN_free(y_bn);
    }

    return NJS_ERROR;
}


static njs_int_t
njs_export_raw_ec(njs_vm_t *vm, njs_webcrypto_key_t *key, njs_value_t *retval)
{
    size_t                   size;
    u_char                   *dst;
    const EC_KEY             *ec;
    const EC_GROUP           *group;
    const EC_POINT           *point;
    point_conversion_form_t  form;

    njs_assert(key->u.a.pkey != NULL);

    if (key->u.a.privat) {
        njs_vm_type_error(vm, "private key of \"%V\" cannot be exported "
                          "in \"raw\" format", njs_algorithm_string(key->alg));
        return NJS_ERROR;
    }

    ec = njs_pkey_get_ec_key(key->u.a.pkey);

    group = EC_KEY_get0_group(ec);
    point = EC_KEY_get0_public_key(ec);
    form = POINT_CONVERSION_UNCOMPRESSED;

    size = EC_POINT_point2oct(group, point, form, NULL, 0, NULL);
    if (njs_slow_path(size == 0)) {
        njs_webcrypto_error(vm, "EC_POINT_point2oct() failed");
        return NJS_ERROR;
    }

    dst = njs_mp_alloc(njs_vm_memory_pool(vm), size);
    if (njs_slow_path(dst == NULL)) {
        return NJS_ERROR;
    }

    size = EC_POINT_point2oct(group, point, form, dst, size, NULL);
    if (njs_slow_path(size == 0)) {
        njs_webcrypto_error(vm, "EC_POINT_point2oct() failed");
        return NJS_ERROR;
    }

    return njs_vm_value_array_buffer_set(vm, retval, dst, size);
}


static njs_int_t
njs_export_jwk_asymmetric(njs_vm_t *vm, njs_webcrypto_key_t *key,
    njs_value_t *retval)
{
    njs_int_t           ret;
    njs_opaque_value_t  ops, extractable;

    njs_assert(key->u.a.pkey != NULL);

    switch (EVP_PKEY_id(key->u.a.pkey)) {
    case EVP_PKEY_RSA:
#if (OPENSSL_VERSION_NUMBER >= 0x10101001L)
    case EVP_PKEY_RSA_PSS:
#endif
        ret = njs_export_jwk_rsa(vm, key, retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        break;

    case EVP_PKEY_EC:
        ret = njs_export_jwk_ec(vm, key, retval);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        break;

    default:
        njs_vm_type_error(vm, "provided key cannot be exported as JWK");
        return NJS_ERROR;
    }

    ret = njs_key_ops(vm, njs_value_arg(&ops), key->usage);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_prop_set(vm, retval, &key_ops, &ops);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_value_boolean_set(njs_value_arg(&extractable), key->extractable);

    return njs_vm_object_prop_set(vm, retval, &string_ext, &extractable);
}


static njs_int_t
njs_export_jwk_oct(njs_vm_t *vm, njs_webcrypto_key_t *key, njs_value_t *retval)
{
    njs_int_t            ret;
    njs_str_t            *nm;
    njs_opaque_value_t   k, alg, ops, extractable, oct_s;
    njs_webcrypto_alg_t  type;

    njs_assert(key->u.s.raw.start != NULL);

    ret = njs_string_base64url(vm, njs_value_arg(&k), &key->u.s.raw);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    type = key->alg->type;

    if (key->alg->type == NJS_ALGORITHM_HMAC) {
        nm = &njs_webcrypto_alg_name[type][key->hash];
        (void) njs_vm_value_string_create(vm, njs_value_arg(&alg), nm->start,
                                          nm->length);

    } else {
        switch (key->u.s.raw.length) {
        case 16:
        case 24:
        case 32:
            nm = &njs_webcrypto_alg_aes_name
                 [type - NJS_ALGORITHM_AES_GCM][(key->u.s.raw.length - 16) / 8];
            (void) njs_vm_value_string_create(vm, njs_value_arg(&alg),
                                              nm->start, nm->length);
            break;

        default:
            njs_value_undefined_set(njs_value_arg(&alg));
            break;
        }
    }

    ret = njs_key_ops(vm, njs_value_arg(&ops), key->usage);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_value_boolean_set(njs_value_arg(&extractable), key->extractable);

    ret = njs_vm_object_alloc(vm, retval, NULL);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_vm_value_string_create(vm, njs_value_arg(&oct_s), (u_char *) "oct", 3);

    ret = njs_vm_object_prop_set(vm, retval, &string_kty, &oct_s);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_prop_set(vm, retval, &string_k, &k);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_prop_set(vm, retval, &key_ops, &ops);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    ret = njs_vm_object_prop_set(vm, retval, &string_ext, &extractable);
    if (ret != NJS_OK) {
        return NJS_ERROR;
    }

    if (!njs_value_is_undefined(njs_value_arg(&alg))) {
        ret = njs_vm_object_prop_set(vm, retval, &string_alg, &alg);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_ext_export_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    BIO                         *bio;
    BUF_MEM                     *mem;
    njs_int_t                   ret;
    njs_webcrypto_key_t         *key;
    PKCS8_PRIV_KEY_INFO         *pkcs8;
    njs_opaque_value_t          value;
    njs_webcrypto_key_format_t  fmt;

    fmt = njs_key_format(vm, njs_arg(args, nargs, 1));
    if (njs_slow_path(fmt == NJS_KEY_FORMAT_UNKNOWN)) {
        goto fail;
    }

    key = njs_vm_external(vm, njs_webcrypto_crypto_key_proto_id,
                          njs_arg(args, nargs, 2));
    if (njs_slow_path(key == NULL)) {
        njs_vm_type_error(vm, "\"key\" is not a CryptoKey object");
        goto fail;
    }

    if (njs_slow_path(!(fmt & key->alg->fmt))) {
        njs_vm_type_error(vm, "unsupported key fmt \"%V\" for \"%V\" key",
                          njs_format_string(fmt),
                          njs_algorithm_string(key->alg));
        goto fail;
    }

    if (njs_slow_path(!key->extractable)) {
        njs_vm_type_error(vm, "provided key cannot be extracted");
        goto fail;
    }

    switch (fmt) {
    case NJS_KEY_FORMAT_JWK:
        switch (key->alg->type) {
        case NJS_ALGORITHM_RSASSA_PKCS1_v1_5:
        case NJS_ALGORITHM_RSA_PSS:
        case NJS_ALGORITHM_RSA_OAEP:
        case NJS_ALGORITHM_ECDSA:
            ret = njs_export_jwk_asymmetric(vm, key, njs_value_arg(&value));
            if (njs_slow_path(ret != NJS_OK)) {
                goto fail;
            }

            break;

        case NJS_ALGORITHM_AES_GCM:
        case NJS_ALGORITHM_AES_CTR:
        case NJS_ALGORITHM_AES_CBC:
        case NJS_ALGORITHM_HMAC:
            ret = njs_export_jwk_oct(vm, key, njs_value_arg(&value));
            if (njs_slow_path(ret != NJS_OK)) {
                goto fail;
            }

            break;

        default:
            break;
        }

        break;

    case NJS_KEY_FORMAT_PKCS8:
        if (!key->u.a.privat) {
            njs_vm_type_error(vm, "public key of \"%V\" cannot be exported "
                              "as PKCS8", njs_algorithm_string(key->alg));
            goto fail;
        }

        bio = BIO_new(BIO_s_mem());
        if (njs_slow_path(bio == NULL)) {
            njs_webcrypto_error(vm, "BIO_new(BIO_s_mem()) failed");
            goto fail;
        }

        njs_assert(key->u.a.pkey != NULL);

        pkcs8 = EVP_PKEY2PKCS8(key->u.a.pkey);
        if (njs_slow_path(pkcs8 == NULL)) {
            BIO_free(bio);
            njs_webcrypto_error(vm, "EVP_PKEY2PKCS8() failed");
            goto fail;
        }

        if (!i2d_PKCS8_PRIV_KEY_INFO_bio(bio, pkcs8)) {
            BIO_free(bio);
            PKCS8_PRIV_KEY_INFO_free(pkcs8);
            njs_webcrypto_error(vm, "i2d_PKCS8_PRIV_KEY_INFO_bio() failed");
            goto fail;
        }

        BIO_get_mem_ptr(bio, &mem);

        ret = njs_webcrypto_array_buffer(vm, njs_value_arg(&value),
                                         (u_char *) mem->data, mem->length);

        BIO_free(bio);
        PKCS8_PRIV_KEY_INFO_free(pkcs8);

        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        break;

    case NJS_KEY_FORMAT_SPKI:
        if (key->u.a.privat) {
            njs_vm_type_error(vm, "private key of \"%V\" cannot be exported "
                              "as SPKI", njs_algorithm_string(key->alg));
            goto fail;
        }

        bio = BIO_new(BIO_s_mem());
        if (njs_slow_path(bio == NULL)) {
            njs_webcrypto_error(vm, "BIO_new(BIO_s_mem()) failed");
            goto fail;
        }

        njs_assert(key->u.a.pkey != NULL);

        if (!i2d_PUBKEY_bio(bio, key->u.a.pkey)) {
            BIO_free(bio);
            njs_webcrypto_error(vm, "i2d_PUBKEY_bio() failed");
            goto fail;
        }

        BIO_get_mem_ptr(bio, &mem);

        ret = njs_webcrypto_array_buffer(vm, njs_value_arg(&value),
                                         (u_char *) mem->data, mem->length);

        BIO_free(bio);

        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        break;

    case NJS_KEY_FORMAT_RAW:
    default:
        if (key->alg->type == NJS_ALGORITHM_ECDSA) {
            ret = njs_export_raw_ec(vm, key, njs_value_arg(&value));
            if (njs_slow_path(ret != NJS_OK)) {
                goto fail;
            }

            break;
        }

        ret = njs_vm_value_array_buffer_set(vm, njs_value_arg(&value),
                                            key->u.s.raw.start,
                                            key->u.s.raw.length);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        break;
    }

    return njs_webcrypto_result(vm, &value, NJS_OK, retval);

fail:

    return njs_webcrypto_result(vm, NULL, NJS_ERROR, retval);
}


static njs_int_t
njs_ext_generate_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    int                        nid;
    unsigned                   usage;
    njs_int_t                  ret;
    njs_bool_t                 extractable;
    njs_value_t                *aobject, *val;
    EVP_PKEY_CTX               *ctx;
    njs_webcrypto_key_t        *key, *keypub;
    njs_opaque_value_t         value, pub, priv;
    njs_webcrypto_algorithm_t  *alg;

    static const njs_str_t  string_priv = njs_str("privateKey");
    static const njs_str_t  string_pub = njs_str("publicKey");

    ctx = NULL;

    aobject = njs_arg(args, nargs, 1);
    extractable = njs_value_bool(njs_arg(args, nargs, 2));

    alg = njs_key_algorithm(vm, aobject);
    if (njs_slow_path(alg == NULL)) {
        goto fail;
    }

    ret = njs_key_usage(vm, njs_arg(args, nargs, 3), &usage);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    key = njs_webcrypto_key_alloc(vm, alg, usage, extractable);
    if (njs_slow_path(key == NULL)) {
        goto fail;
    }

    if (njs_slow_path(usage & ~alg->usage)) {
        njs_vm_type_error(vm, "unsupported key usage for \"%V\" key",
                          njs_algorithm_string(alg));
        goto fail;
    }

    switch (alg->type) {
    case NJS_ALGORITHM_RSASSA_PKCS1_v1_5:
    case NJS_ALGORITHM_RSA_PSS:
    case NJS_ALGORITHM_RSA_OAEP:
        ret = njs_algorithm_hash(vm, aobject, &key->hash);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        val = njs_vm_object_prop(vm, aobject, &string_ml, &value);
        if (njs_slow_path(val == NULL)) {
            goto fail;
        }

        if (!njs_value_is_number(val)) {
            njs_vm_type_error(vm, "\"modulusLength\" is not a number");
            goto fail;
        }

        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
        if (njs_slow_path(ctx == NULL)) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_new_id() failed");
            goto fail;
        }

        if (EVP_PKEY_keygen_init(ctx) <= 0) {
            njs_webcrypto_error(vm, "EVP_PKEY_keygen_init() failed");
            goto fail;
        }

        if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, njs_value_number(val)) <= 0) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_set_rsa_keygen_bits() "
                                "failed");
            goto fail;
        }

        if (EVP_PKEY_keygen(ctx, &key->u.a.pkey) <= 0) {
            njs_webcrypto_error(vm, "EVP_PKEY_keygen() failed");
            goto fail;
        }

        EVP_PKEY_CTX_free(ctx);
        ctx = NULL;

        key->u.a.privat = 1;
        key->usage = (alg->type == NJS_ALGORITHM_RSA_OAEP)
                        ? NJS_KEY_USAGE_DECRYPT
                        : NJS_KEY_USAGE_SIGN;

        keypub = njs_webcrypto_key_alloc(vm, alg, usage, extractable);
        if (njs_slow_path(keypub == NULL)) {
            goto fail;
        }

        if (njs_pkey_up_ref(key->u.a.pkey) <= 0) {
            njs_webcrypto_error(vm, "njs_pkey_up_ref() failed");
            goto fail;
        }

        keypub->u.a.pkey = key->u.a.pkey;
        keypub->hash = key->hash;
        keypub->usage = (alg->type == NJS_ALGORITHM_RSA_OAEP)
                          ? NJS_KEY_USAGE_ENCRYPT
                          : NJS_KEY_USAGE_VERIFY;

        ret = njs_vm_external_create(vm, njs_value_arg(&priv),
                                     njs_webcrypto_crypto_key_proto_id, key, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        ret = njs_vm_external_create(vm, njs_value_arg(&pub),
                                  njs_webcrypto_crypto_key_proto_id, keypub, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        ret = njs_vm_object_alloc(vm, njs_value_arg(&value), NULL);
        if (ret != NJS_OK) {
            goto fail;
        }

        ret = njs_vm_object_prop_set(vm, njs_value_arg(&value), &string_priv,
                                     &priv);
        if (ret != NJS_OK) {
            goto fail;
        }

        ret = njs_vm_object_prop_set(vm, njs_value_arg(&value), &string_pub,
                                     &pub);
        if (ret != NJS_OK) {
            goto fail;
        }

        break;

    case NJS_ALGORITHM_ECDSA:
        nid = 0;
        ret = njs_algorithm_curve(vm, aobject, &nid);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
        if (njs_slow_path(ctx == NULL)) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_new_id() failed");
            goto fail;
        }

        if (EVP_PKEY_keygen_init(ctx) <= 0) {
            njs_webcrypto_error(vm, "EVP_PKEY_keygen_init() failed");
            goto fail;
        }

        if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, nid) <= 0) {
            njs_webcrypto_error(vm, "EVP_PKEY_CTX_set_ec_paramgen_curve_nid() "
                                "failed");
            goto fail;
        }

        if (EVP_PKEY_keygen(ctx, &key->u.a.pkey) <= 0) {
            njs_webcrypto_error(vm, "EVP_PKEY_keygen() failed");
            goto fail;
        }

        EVP_PKEY_CTX_free(ctx);
        ctx = NULL;

        key->u.a.privat = 1;
        key->usage = NJS_KEY_USAGE_SIGN;

        keypub = njs_webcrypto_key_alloc(vm, alg, usage, extractable);
        if (njs_slow_path(keypub == NULL)) {
            goto fail;
        }

        if (njs_pkey_up_ref(key->u.a.pkey) <= 0) {
            njs_webcrypto_error(vm, "njs_pkey_up_ref() failed");
            goto fail;
        }

        keypub->u.a.pkey = key->u.a.pkey;
        keypub->u.a.curve = key->u.a.curve;
        keypub->usage = NJS_KEY_USAGE_VERIFY;

        ret = njs_vm_external_create(vm, njs_value_arg(&priv),
                                     njs_webcrypto_crypto_key_proto_id, key, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        ret = njs_vm_external_create(vm, njs_value_arg(&pub),
                                  njs_webcrypto_crypto_key_proto_id, keypub, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        ret = njs_vm_object_alloc(vm, njs_value_arg(&value), NULL);
        if (ret != NJS_OK) {
            goto fail;
        }

        ret = njs_vm_object_prop_set(vm, njs_value_arg(&value), &string_priv,
                                     &priv);
        if (ret != NJS_OK) {
            goto fail;
        }

        ret = njs_vm_object_prop_set(vm, njs_value_arg(&value), &string_pub,
                                     &pub);
        if (ret != NJS_OK) {
            goto fail;
        }

        break;

    case NJS_ALGORITHM_AES_GCM:
    case NJS_ALGORITHM_AES_CTR:
    case NJS_ALGORITHM_AES_CBC:
    case NJS_ALGORITHM_HMAC:

        if (alg->type == NJS_ALGORITHM_HMAC) {
            ret = njs_algorithm_hash(vm, aobject, &key->hash);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto fail;
            }

            key->u.s.raw.length =
                              EVP_MD_size(njs_algorithm_hash_digest(key->hash));

        } else {
            val = njs_vm_object_prop(vm, aobject, &string_length, &value);
            if (val != NULL) {
                key->u.s.raw.length = njs_value_number(val) / 8;

                if (key->u.s.raw.length != 16
                    && key->u.s.raw.length != 24
                    && key->u.s.raw.length != 32)
                {
                    njs_vm_type_error(vm, "length for \"%V\" key should be "
                                      "one of 128, 192, 256",
                                      njs_algorithm_string(alg));
                    goto fail;
                }
            }
        }

        key->u.s.raw.start = njs_mp_alloc(njs_vm_memory_pool(vm),
                                          key->u.s.raw.length);
        if (njs_slow_path(key->u.s.raw.start == NULL)) {
            njs_vm_memory_error(vm);
            goto fail;
        }

        if (RAND_bytes(key->u.s.raw.start, key->u.s.raw.length) <= 0) {
            njs_webcrypto_error(vm, "RAND_bytes() failed");
            goto fail;
        }

        ret = njs_vm_external_create(vm, njs_value_arg(&value),
                                     njs_webcrypto_crypto_key_proto_id, key, 0);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        break;

    default:
        njs_vm_internal_error(vm, "not implemented generateKey"
                              "algorithm: \"%V\"", njs_algorithm_string(alg));
        return NJS_ERROR;
    }

    return njs_webcrypto_result(vm, &value, NJS_OK, retval);

fail:

    if (ctx != NULL) {
        EVP_PKEY_CTX_free(ctx);
    }

    return njs_webcrypto_result(vm, NULL, NJS_ERROR, retval);
}


static BIGNUM *
njs_import_base64url_bignum(njs_vm_t *vm, njs_opaque_value_t *value)
{
    njs_int_t  ret;
    njs_str_t  data, decoded;
    u_char     buf[512];

    ret = njs_vm_value_to_bytes(vm, &data, njs_value_arg(value));
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    (void) njs_decode_base64url_length(&data, &decoded.length);

    if (njs_slow_path(decoded.length > sizeof(buf))) {
        return NULL;
    }

    decoded.start = buf;

    njs_decode_base64url(&decoded, &data);

    return BN_bin2bn(decoded.start, decoded.length, NULL);
}


static EVP_PKEY *
njs_import_jwk_rsa(njs_vm_t *vm, njs_value_t *jwk, njs_webcrypto_key_t *key)
{
    RSA                    *rsa;
    BIGNUM                 *n_bn, *e_bn, *d_bn, *p_bn, *q_bn, *dp_bn, *dq_bn,
                           *qi_bn;
    njs_str_t              alg;
    unsigned               usage;
    EVP_PKEY               *pkey;
    njs_int_t              ret;
    njs_value_t            *val;
    njs_opaque_value_t     n, e, d, p, q, dp, dq, qi, value;
    njs_webcrypto_entry_t  *w;

    val = njs_vm_object_prop(vm, jwk, &string_n, &n);
    if (njs_slow_path(val == NULL)) {
        goto fail0;
    }

    val = njs_vm_object_prop(vm, jwk, &string_e, &e);
    if (njs_slow_path(val == NULL)) {
        goto fail0;
    }

    val = njs_vm_object_prop(vm, jwk, &string_d, &d);
    if (njs_slow_path(val == NULL)) {
        njs_value_undefined_set(njs_value_arg(&d));
    }

    if (!njs_value_is_string(njs_value_arg(&n))
        || !njs_value_is_string(njs_value_arg(&e))
        || (!njs_value_is_undefined(njs_value_arg(&d))
            && !njs_value_is_string(njs_value_arg(&d))))
    {
fail0:
        njs_vm_type_error(vm, "Invalid JWK RSA key");
        return NULL;
    }

    key->u.a.privat = njs_value_is_string(njs_value_arg(&d));

    val = njs_vm_object_prop(vm, jwk, &key_ops, &value);
    if (val != NULL && !njs_value_is_undefined(val)){
        ret = njs_key_usage(vm, val, &usage);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        if ((key->usage & usage) != key->usage) {
            njs_vm_type_error(vm, "Key operations and usage mismatch");
            return NULL;
        }
    }

    val = njs_vm_object_prop(vm, jwk, &string_alg, &value);
    if (val != NULL && !njs_value_is_undefined(val)){
        ret = njs_value_to_string(vm, val, val);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        njs_value_string_get(val, &alg);

        for (w = &njs_webcrypto_alg_hash[0]; w->name.length != 0; w++) {
            if (njs_strstr_eq(&alg, &w->name)) {
                key->hash = w->value;
                break;
            }
        }
    }

    if (key->extractable) {
        val = njs_vm_object_prop(vm, jwk, &string_ext, &value);
        if (val != NULL
            && !njs_value_is_undefined(val)
            && !njs_value_bool(val))
        {
            njs_vm_type_error(vm, "JWK RSA is not extractable");
            return NULL;
        }
    }

    rsa = RSA_new();
    if (rsa == NULL) {
        njs_webcrypto_error(vm, "RSA_new() failed");
        return NULL;
    }

    n_bn = njs_import_base64url_bignum(vm, &n);
    if (njs_slow_path(n_bn == NULL)) {
        goto fail;
    }

    e_bn = njs_import_base64url_bignum(vm, &e);
    if (njs_slow_path(e_bn == NULL)) {
        goto fail;
    }

    if (!njs_rsa_set0_key(rsa, n_bn, e_bn, NULL)) {
        BN_free(n_bn);
        BN_free(e_bn);

        njs_webcrypto_error(vm, "RSA_set0_key() failed");
        goto fail;
    }

    if (!key->u.a.privat) {
        goto done;
    }

    val = njs_vm_object_prop(vm, jwk, &string_p, &p);
    if (njs_slow_path(val == NULL)) {
        goto fail1;
    }

    val = njs_vm_object_prop(vm, jwk, &string_q, &q);
    if (njs_slow_path(val == NULL)) {
        goto fail1;
    }

    val = njs_vm_object_prop(vm, jwk, &string_dp, &dp);
    if (njs_slow_path(val == NULL)) {
        goto fail1;
    }

    val = njs_vm_object_prop(vm, jwk, &string_dq, &dq);
    if (njs_slow_path(val == NULL)) {
        goto fail1;
    }

    val = njs_vm_object_prop(vm, jwk, &string_qi, &qi);
    if (njs_slow_path(val == NULL)) {
        goto fail1;
    }

    if (!njs_value_is_string(njs_value_arg(&d))
        || !njs_value_is_string(njs_value_arg(&p))
        || !njs_value_is_string(njs_value_arg(&q))
        || !njs_value_is_string(njs_value_arg(&dp))
        || !njs_value_is_string(njs_value_arg(&dq))
        || !njs_value_is_string(njs_value_arg(&qi)))
    {
fail1:
        njs_vm_type_error(vm, "Invalid JWK RSA key");
        goto fail;
    }

    d_bn = njs_import_base64url_bignum(vm, &d);
    if (njs_slow_path(d_bn == NULL)) {
        goto fail;
    }

    if (!njs_rsa_set0_key(rsa, NULL, NULL, d_bn)) {
        BN_free(d_bn);

        njs_webcrypto_error(vm, "RSA_set0_key() failed");
        goto fail;
    }

    p_bn = njs_import_base64url_bignum(vm, &p);
    if (njs_slow_path(p_bn == NULL)) {
        goto fail;
    }

    q_bn = njs_import_base64url_bignum(vm, &q);
    if (njs_slow_path(q_bn == NULL)) {
        BN_free(p_bn);
        goto fail;
    }

    if (!njs_rsa_set0_factors(rsa, p_bn, q_bn)) {
        BN_free(p_bn);
        BN_free(q_bn);

        njs_webcrypto_error(vm, "RSA_set0_factors() failed");
        goto fail;
    }

    dp_bn = njs_import_base64url_bignum(vm, &dp);
    if (njs_slow_path(dp_bn == NULL)) {
        goto fail;
    }

    dq_bn = njs_import_base64url_bignum(vm, &dq);
    if (njs_slow_path(dq_bn == NULL)) {
        BN_free(dp_bn);
        goto fail;
    }

    qi_bn = njs_import_base64url_bignum(vm, &qi);
    if (njs_slow_path(qi_bn == NULL)) {
        BN_free(dp_bn);
        BN_free(dq_bn);
        goto fail;
    }

    if (!njs_rsa_set0_ctr_params(rsa, dp_bn, dq_bn, qi_bn)) {
        BN_free(dp_bn);
        BN_free(dq_bn);
        BN_free(qi_bn);
        njs_webcrypto_error(vm, "RSA_set0_crt_params() failed");
        goto fail;
    }

done:

    pkey = EVP_PKEY_new();
    if (njs_slow_path(pkey == NULL)) {
        goto fail;
    }

    if (!EVP_PKEY_set1_RSA(pkey, rsa)) {
        EVP_PKEY_free(pkey);
        goto fail;
    }

    RSA_free(rsa);

    return pkey;

fail:

    RSA_free(rsa);

    return NULL;
}


static EVP_PKEY *
njs_import_raw_ec(njs_vm_t *vm, njs_str_t *data, njs_webcrypto_key_t *key)
{
    EC_KEY          *ec;
    EVP_PKEY        *pkey;
    EC_POINT        *pub;
    const EC_GROUP  *group;

    ec = EC_KEY_new_by_curve_name(key->u.a.curve);
    if (njs_slow_path(ec == NULL)) {
        njs_webcrypto_error(vm, "EC_KEY_new_by_curve_name() failed");
        return NULL;
    }

    group = EC_KEY_get0_group(ec);

    pub = EC_POINT_new(group);
    if (njs_slow_path(pub == NULL)) {
        EC_KEY_free(ec);
        njs_webcrypto_error(vm, "EC_POINT_new() failed");
        return NULL;
    }

    if (!EC_POINT_oct2point(group, pub, data->start, data->length, NULL)) {
        EC_KEY_free(ec);
        EC_POINT_free(pub);
        njs_webcrypto_error(vm, "EC_POINT_oct2point() failed");
        return NULL;
    }

    if (!EC_KEY_set_public_key(ec, pub)) {
        EC_KEY_free(ec);
        EC_POINT_free(pub);
        njs_webcrypto_error(vm, "EC_KEY_set_public_key() failed");
        return NULL;
    }

    pkey = EVP_PKEY_new();
    if (njs_slow_path(pkey == NULL)) {
        EC_KEY_free(ec);
        EC_POINT_free(pub);
        njs_webcrypto_error(vm, "EVP_PKEY_new() failed");
        return NULL;
    }

    if (!EVP_PKEY_set1_EC_KEY(pkey, ec)) {
        EC_KEY_free(ec);
        EC_POINT_free(pub);
        EVP_PKEY_free(pkey);
        njs_webcrypto_error(vm, "EVP_PKEY_set1_EC_KEY() failed");
        return NULL;
    }

    EC_KEY_free(ec);
    EC_POINT_free(pub);

    return pkey;
}


static EVP_PKEY *
njs_import_jwk_ec(njs_vm_t *vm, njs_value_t *jwk, njs_webcrypto_key_t *key)
{
    int                    curve;
    EC_KEY                 *ec;
    BIGNUM                 *x_bn, *y_bn, *d_bn;
    unsigned               usage;
    EVP_PKEY               *pkey;
    njs_str_t              name;
    njs_int_t              ret;
    njs_value_t            *val;
    njs_opaque_value_t     x, y, d, value;
    njs_webcrypto_entry_t  *e;

    ec = NULL;
    x_bn = NULL;
    y_bn = NULL;
    d_bn = NULL;

    val = njs_vm_object_prop(vm, jwk, &string_x, &x);
    if (njs_slow_path(val == NULL)) {
        goto fail0;
    }

    val = njs_vm_object_prop(vm, jwk, &string_y, &y);
    if (njs_slow_path(val == NULL)) {
        goto fail0;
    }

    val = njs_vm_object_prop(vm, jwk, &string_d, &d);
    if (njs_slow_path(val == NULL)) {
        njs_value_undefined_set(njs_value_arg(&d));
    }

    if (!njs_value_is_string(njs_value_arg(&x))
        || !njs_value_is_string(njs_value_arg(&y))
        || (!njs_value_is_undefined(njs_value_arg(&d))
            && !njs_value_is_string(njs_value_arg(&d))))
    {
fail0:
        njs_vm_type_error(vm, "Invalid JWK EC key");
        return NULL;
    }

    key->u.a.privat = njs_value_is_string(njs_value_arg(&d));

    val = njs_vm_object_prop(vm, jwk, &key_ops, &value);
    if (val != NULL && !njs_value_is_undefined(val)) {
        ret = njs_key_usage(vm, val, &usage);
        if (njs_slow_path(ret != NJS_OK)) {
            return NULL;
        }

        if ((key->usage & usage) != key->usage) {
            njs_vm_type_error(vm, "Key operations and usage mismatch");
            return NULL;
        }
    }

    if (key->extractable) {
        val = njs_vm_object_prop(vm, jwk, &string_ext, &value);
        if (val != NULL
            && !njs_value_is_undefined(val)
            && !njs_value_bool(val))
        {
            njs_vm_type_error(vm, "JWK EC is not extractable");
            return NULL;
        }
    }

    curve = 0;

    val = njs_vm_object_prop(vm, jwk, &string_crv, &value);
    if (val != NULL && !njs_value_is_undefined(val)) {
        njs_value_string_get(val, &name);

        for (e = &njs_webcrypto_curve[0]; e->name.length != 0; e++) {
            if (njs_strstr_eq(&name, &e->name)) {
                curve = e->value;
                break;
            }
        }
    }

    if (curve != key->u.a.curve) {
        njs_vm_type_error(vm, "JWK EC curve mismatch");
        return NULL;
    }

    ec = EC_KEY_new_by_curve_name(key->u.a.curve);
    if (njs_slow_path(ec == NULL)) {
        njs_webcrypto_error(vm, "EC_KEY_new_by_curve_name() failed");
        return NULL;
    }

    x_bn = njs_import_base64url_bignum(vm, &x);
    if (njs_slow_path(x_bn == NULL)) {
        goto fail;
    }

    y_bn = njs_import_base64url_bignum(vm, &y);
    if (njs_slow_path(y_bn == NULL)) {
        goto fail;
    }

    if (key->u.a.privat) {
        d_bn = njs_import_base64url_bignum(vm, &d);
        if (njs_slow_path(d_bn == NULL)) {
            goto fail;
        }
    }

    if (!EC_KEY_set_public_key_affine_coordinates(ec, x_bn, y_bn)) {
        njs_webcrypto_error(vm, "EC_KEY_set_public_key_affine_coordinates() "
                            "failed");
        goto fail;
    }

    BN_free(x_bn);
    x_bn = NULL;

    BN_free(y_bn);
    y_bn = NULL;

    pkey = EVP_PKEY_new();
    if (njs_slow_path(pkey == NULL)) {
        goto fail;
    }

    if (!EVP_PKEY_set1_EC_KEY(pkey, ec)) {
        njs_webcrypto_error(vm, "EVP_PKEY_set1_EC_KEY() failed");
        goto fail_pkey;
    }

    if (key->u.a.privat) {
        if (!EC_KEY_set_private_key(ec, d_bn)) {
            njs_webcrypto_error(vm, "EC_KEY_set_private_key() failed");
            goto fail_pkey;
        }

        BN_free(d_bn);
        d_bn = NULL;
    }

    EC_KEY_free(ec);

    return pkey;

fail_pkey:

    EVP_PKEY_free(pkey);
    EC_KEY_free(ec);
    ec = NULL;

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

    return NULL;
}


static njs_int_t
njs_import_jwk_oct(njs_vm_t *vm, njs_value_t *jwk, njs_webcrypto_key_t *key)
{
    size_t                 size;
    unsigned               usage;
    njs_int_t              ret;
    njs_str_t              *a, alg, b64;
    njs_value_t            *val;
    njs_opaque_value_t     value;
    njs_webcrypto_alg_t    type;
    njs_webcrypto_entry_t  *w;

    static njs_webcrypto_entry_t hashes[] = {
        { njs_str("HS1"), NJS_HASH_SHA1 },
        { njs_str("HS256"), NJS_HASH_SHA256 },
        { njs_str("HS384"), NJS_HASH_SHA384 },
        { njs_str("HS512"), NJS_HASH_SHA512 },
        { njs_null_str, 0 }
    };

    val = njs_vm_object_prop(vm, jwk, &string_k, &value);
    if (njs_slow_path(val == NULL || !njs_value_is_string(val))) {
        njs_vm_type_error(vm, "Invalid JWK oct key");
        return NJS_ERROR;
    }

    njs_value_string_get(val, &b64);

    (void) njs_decode_base64url_length(&b64, &key->u.s.raw.length);

    key->u.s.raw.start = njs_mp_alloc(njs_vm_memory_pool(vm),
                                      key->u.s.raw.length);
    if (njs_slow_path(key->u.s.raw.start == NULL)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    njs_decode_base64url(&key->u.s.raw, &b64);

    val = njs_vm_object_prop(vm, jwk, &string_alg, &value);
    if (njs_slow_path(val == NULL || !njs_value_is_string(val))) {
        njs_vm_type_error(vm, "Invalid JWK oct alg");
        return NJS_ERROR;
    }

    njs_value_string_get(val, &alg);

    size = 16;

    if (key->alg->type == NJS_ALGORITHM_HMAC) {
        for (w = &hashes[0]; w->name.length != 0; w++) {
            if (njs_strstr_eq(&alg, &w->name)) {
                key->hash = w->value;
                goto done;
            }
        }

    } else {
        type = key->alg->type;
        a = &njs_webcrypto_alg_aes_name[type - NJS_ALGORITHM_AES_GCM][0];
        for (; a->length != 0; a++) {
            if (njs_strstr_eq(&alg, a)) {
                goto done;
            }

            size += 8;
        }
    }

    njs_vm_type_error(vm, "unexpected \"alg\" value \"%V\" for JWK key",
                      &alg);
    return NJS_ERROR;

done:

    if (key->alg->type != NJS_ALGORITHM_HMAC) {
        if (key->u.s.raw.length != size) {
            njs_vm_type_error(vm, "key size and \"alg\" value \"%V\" mismatch",
                              &alg);
            return NJS_ERROR;
        }
    }

    val = njs_vm_object_prop(vm, jwk, &key_ops, &value);
    if (val != NULL && !njs_value_is_undefined(val)) {
        ret = njs_key_usage(vm, val, &usage);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        if ((key->usage & usage) != key->usage) {
            njs_vm_type_error(vm, "Key operations and usage mismatch");
            return NJS_ERROR;
        }
    }

    if (key->extractable) {
        val = njs_vm_object_prop(vm, jwk, &string_ext, &value);
        if (val != NULL
            && !njs_value_is_undefined(val)
            && !njs_value_bool(val))
        {
            njs_vm_type_error(vm, "JWK oct is not extractable");
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static njs_int_t
njs_ext_import_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    int                         nid;
    BIO                         *bio;
#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
    RSA                         *rsa;
    EC_KEY                      *ec;
#else
    char                        gname[80];
#endif
    unsigned                    mask, usage;
    EVP_PKEY                    *pkey;
    njs_int_t                   ret;
    njs_str_t                   key_data, kty;
    njs_value_t                 *options, *jwk, *val;
    const u_char                *start;
#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
    const EC_GROUP              *group;
#endif
    njs_webcrypto_key_t         *key;
    PKCS8_PRIV_KEY_INFO         *pkcs8;
    njs_opaque_value_t          value;
    njs_webcrypto_hash_t        hash;
    njs_webcrypto_algorithm_t   *alg;
    njs_webcrypto_key_format_t  fmt;

    pkey = NULL;
    key_data.start = NULL;
    key_data.length = 0;

    fmt = njs_key_format(vm, njs_arg(args, nargs, 1));
    if (njs_slow_path(fmt == NJS_KEY_FORMAT_UNKNOWN)) {
        goto fail;
    }

    options = njs_arg(args, nargs, 3);
    alg = njs_key_algorithm(vm, options);
    if (njs_slow_path(alg == NULL)) {
        goto fail;
    }

    if (njs_slow_path(!(fmt & alg->fmt))) {
        njs_vm_type_error(vm, "unsupported key fmt \"%V\" for \"%V\" key",
                          njs_format_string(fmt),
                          njs_algorithm_string(alg));
        goto fail;
    }

    ret = njs_key_usage(vm, njs_arg(args, nargs, 5), &usage);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    if (njs_slow_path(usage & ~alg->usage)) {
        njs_vm_type_error(vm, "unsupported key usage for \"%V\" key",
                          njs_algorithm_string(alg));
        goto fail;
    }

    if (fmt != NJS_KEY_FORMAT_JWK) {
        ret = njs_vm_value_to_bytes(vm, &key_data, njs_arg(args, nargs, 2));
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }
    }

    key = njs_webcrypto_key_alloc(vm, alg, usage,
                                  njs_value_bool(njs_arg(args, nargs, 4)));
    if (njs_slow_path(key == NULL)) {
        goto fail;
    }

    /*
     * set by njs_webcrypto_key_alloc():
     *
     *  key->u.a.pkey = NULL;
     *  key->u.s.raw.length = 0;
     *  key->u.s.raw.start = NULL;
     *  key->u.a.curve = 0;
     *  key->u.a.privat = 0;
     *  key->hash = NJS_HASH_UNSET;
     */

    switch (fmt) {
    case NJS_KEY_FORMAT_PKCS8:
        bio = njs_bio_new_mem_buf(key_data.start, key_data.length);
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

        key->u.a.privat = 1;

        break;

    case NJS_KEY_FORMAT_SPKI:
        start = key_data.start;
        pkey = d2i_PUBKEY(NULL, &start, key_data.length);
        if (njs_slow_path(pkey == NULL)) {
            njs_webcrypto_error(vm, "d2i_PUBKEY() failed");
            goto fail;
        }

        break;

    case NJS_KEY_FORMAT_JWK:
        jwk = njs_arg(args, nargs, 2);
        if (!njs_value_is_object(jwk)) {
            njs_vm_type_error(vm, "invalid JWK key data: object value "
                              "expected");
            goto fail;
        }

        val = njs_vm_object_prop(vm, jwk, &string_kty, &value);
        if (njs_slow_path(val == NULL)) {
            val = njs_value_arg(&njs_value_undefined);
        }

        ret = njs_vm_value_to_bytes(vm, &kty, val);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

        if (njs_strstr_eq(&kty, &njs_str_value("RSA"))) {
            pkey = njs_import_jwk_rsa(vm, jwk, key);
            if (njs_slow_path(pkey == NULL)) {
                goto fail;
            }

        } else if (njs_strstr_eq(&kty, &njs_str_value("EC"))) {
            ret = njs_algorithm_curve(vm, options, &key->u.a.curve);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto fail;
            }

            pkey = njs_import_jwk_ec(vm, jwk, key);
            if (njs_slow_path(pkey == NULL)) {
                goto fail;
            }

        } else if (njs_strstr_eq(&kty, &njs_str_value("oct"))) {
            ret = njs_import_jwk_oct(vm, jwk, key);
            if (njs_slow_path(ret != NJS_OK)) {
                goto fail;
            }

        } else {
            njs_vm_type_error(vm, "invalid JWK key type: %V", &kty);
            goto fail;
        }

        break;

    case NJS_KEY_FORMAT_RAW:
    default:
        break;
    }

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

        ret = njs_algorithm_hash(vm, options, &hash);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        if (key->hash != NJS_HASH_UNSET && key->hash != hash) {
            njs_vm_type_error(vm, "RSA JWK hash mismatch");
            goto fail;
        }

        if (key->u.a.privat) {
            mask = (alg->type == NJS_ALGORITHM_RSA_OAEP)
                         ? ~(NJS_KEY_USAGE_DECRYPT | NJS_KEY_USAGE_UNWRAP_KEY)
                         : ~(NJS_KEY_USAGE_SIGN);
        } else {
            mask = (alg->type == NJS_ALGORITHM_RSA_OAEP)
                         ? ~(NJS_KEY_USAGE_ENCRYPT | NJS_KEY_USAGE_WRAP_KEY)
                         : ~(NJS_KEY_USAGE_VERIFY);
        }

        if (key->usage & mask) {
            njs_vm_type_error(vm, "key usage mismatch for \"%V\" key",
                              njs_algorithm_string(alg));
            goto fail;
        }

        key->hash = hash;
        key->u.a.pkey = pkey;

        break;

    case NJS_ALGORITHM_ECDSA:
    case NJS_ALGORITHM_ECDH:
        ret = njs_algorithm_curve(vm, options, &key->u.a.curve);
        if (njs_slow_path(ret == NJS_ERROR)) {
            goto fail;
        }

        if (fmt == NJS_KEY_FORMAT_RAW) {
            pkey = njs_import_raw_ec(vm, &key_data, key);
            if (njs_slow_path(pkey == NULL)) {
                goto fail;
            }
        }

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

        if (njs_slow_path(key->u.a.curve != nid)) {
            njs_webcrypto_error(vm, "name curve mismatch");
            goto fail;
        }

        mask = key->u.a.privat ? ~NJS_KEY_USAGE_SIGN : ~NJS_KEY_USAGE_VERIFY;

        if (key->usage & mask) {
            njs_vm_type_error(vm, "key usage mismatch for \"%V\" key",
                              njs_algorithm_string(alg));
            goto fail;
        }

        key->u.a.pkey = pkey;

        break;

    case NJS_ALGORITHM_HMAC:
        if (fmt == NJS_KEY_FORMAT_RAW) {
            ret = njs_algorithm_hash(vm, options, &key->hash);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto fail;
            }

            key->u.s.raw = key_data;

        } else {
            /* NJS_KEY_FORMAT_JWK. */

            ret = njs_algorithm_hash(vm, options, &hash);
            if (njs_slow_path(ret == NJS_ERROR)) {
                goto fail;
            }

            if (key->hash != NJS_HASH_UNSET && key->hash != hash) {
                njs_vm_type_error(vm, "HMAC JWK hash mismatch");
                goto fail;
            }
        }

        break;

    case NJS_ALGORITHM_AES_GCM:
    case NJS_ALGORITHM_AES_CTR:
    case NJS_ALGORITHM_AES_CBC:
        if (fmt == NJS_KEY_FORMAT_RAW) {
            switch (key_data.length) {
            case 16:
            case 24:
            case 32:
                break;

            default:
                njs_vm_type_error(vm, "AES Invalid key length");
                goto fail;
            }

            key->u.s.raw = key_data;
        }

        break;

    case NJS_ALGORITHM_PBKDF2:
    case NJS_ALGORITHM_HKDF:
    default:
        key->u.s.raw = key_data;
        break;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value),
                                 njs_webcrypto_crypto_key_proto_id, key, 0);
    if (njs_slow_path(ret != NJS_OK)) {
        goto fail;
    }

    return njs_webcrypto_result(vm, &value, NJS_OK, retval);

fail:

    if (pkey != NULL) {
        EVP_PKEY_free(pkey);
    }

    return njs_webcrypto_result(vm, NULL, NJS_ERROR, retval);
}


static njs_int_t
njs_set_rsa_padding(njs_vm_t *vm, njs_value_t *options, EVP_PKEY *pkey,
    EVP_PKEY_CTX *ctx, njs_webcrypto_alg_t type)
{
    int                 padding;
    int64_t             salt_length;
    njs_int_t           ret;
    njs_value_t         *value;
    njs_opaque_value_t  lvalue;

    static const njs_str_t  string_saltl = njs_str("saltLength");

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
        value = njs_vm_object_prop(vm, options, &string_saltl, &lvalue);
        if (njs_slow_path(value == NULL)) {
            njs_vm_type_error(vm, "RSA-PSS algorithm.saltLength is not "
                              "provided");
            return NJS_ERROR;
        }

        ret = njs_value_to_integer(vm, value, &salt_length);
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


static unsigned int
njs_ec_rs_size(EVP_PKEY *pkey)
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


static njs_int_t
njs_convert_der_to_p1363(njs_vm_t *vm, EVP_PKEY *pkey, const u_char *der,
    size_t der_len, u_char **pout, size_t *out_len)
{
    u_char        *data;
    unsigned      n;
    njs_int_t     ret;
    ECDSA_SIG     *ec_sig;
    const BIGNUM  *r, *s;

    ret = NJS_OK;
    ec_sig = NULL;

    n = njs_ec_rs_size(pkey);
    if (n == 0) {
        goto fail;
    }

    data = njs_mp_alloc(njs_vm_memory_pool(vm), 2 * n);
    if (njs_slow_path(data == NULL)) {
        goto memory_error;
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

    if (njs_bn_bn2binpad(r, data, n) <= 0) {
        goto fail;
    }

    if (njs_bn_bn2binpad(s, &data[n], n) <= 0) {
        goto fail;
    }

    *pout = data;
    *out_len = 2 * n;

    goto done;

fail:

    *out_len = 0;

done:

    if (ec_sig != NULL) {
        ECDSA_SIG_free(ec_sig);
    }

    return ret;

memory_error:

    njs_vm_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_convert_p1363_to_der(njs_vm_t *vm, EVP_PKEY *pkey, u_char *p1363,
    size_t p1363_len, u_char **pout, size_t *out_len)
{
    int        len;
    BIGNUM     *r, *s;
    u_char     *data;
    unsigned   n;
    njs_int_t  ret;
    ECDSA_SIG  *ec_sig;

    ret = NJS_OK;
    ec_sig = NULL;

    n = njs_ec_rs_size(pkey);

    if (njs_slow_path(n == 0 || p1363_len != 2 * n)) {
        goto fail;
    }

    ec_sig = ECDSA_SIG_new();
    if (njs_slow_path(ec_sig == NULL)) {
        goto memory_error;
    }

    r = BN_new();
    if (njs_slow_path(r == NULL)) {
        goto memory_error;
    }

    s = BN_new();
    if (njs_slow_path(s == NULL)) {
        goto memory_error;
    }

    if (r != BN_bin2bn(p1363, n, r)) {
        goto fail;
    }

    if (s != BN_bin2bn(&p1363[n], n, s)) {
        goto fail;
    }

    if (njs_ecdsa_sig_set0(ec_sig, r, s) != 1) {
        njs_webcrypto_error(vm, "njs_ecdsa_sig_set0() failed");
        ret = NJS_ERROR;
        goto fail;
    }

    data = njs_mp_alloc(njs_vm_memory_pool(vm), 2 * n + 16);
    if (njs_slow_path(data == NULL)) {
        goto memory_error;
    }

    *pout = data;
    len = i2d_ECDSA_SIG(ec_sig, &data);

    if (len < 0) {
        goto fail;
    }

    *out_len = len;

    goto done;

fail:

    *out_len = 0;

done:

    if (ec_sig != NULL) {
        ECDSA_SIG_free(ec_sig);
    }

    return ret;

memory_error:

    njs_vm_memory_error(vm);

    return NJS_ERROR;
}


static njs_int_t
njs_ext_sign(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t verify, njs_value_t *retval)
{
    u_char                     *dst, *p;
    size_t                     olen, outlen;
    unsigned                   mask, m_len;
    njs_int_t                  ret;
    njs_str_t                  data, sig;
    EVP_MD_CTX                 *mctx;
    njs_value_t                *options;
    EVP_PKEY_CTX               *pctx;
    const EVP_MD               *md;
    njs_opaque_value_t         result;
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
        njs_vm_type_error(vm, "\"key\" is not a CryptoKey object");
        goto fail;
    }

    mask = verify ? NJS_KEY_USAGE_VERIFY : NJS_KEY_USAGE_SIGN;
    if (njs_slow_path(!(key->usage & mask))) {
        njs_vm_type_error(vm, "provide key does not support \"sign\" "
                          "operation");
        goto fail;
    }

    if (njs_slow_path(key->alg != alg)) {
        njs_vm_type_error(vm, "cannot %s using \"%V\" with \"%V\" key",
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
        m_len = EVP_MD_size(md);

        if (!verify) {
            dst = njs_mp_alloc(njs_vm_memory_pool(vm), m_len);
            if (njs_slow_path(dst == NULL)) {
                njs_vm_memory_error(vm);
                goto fail;
            }

        } else {
            dst = (u_char *) &m[0];
        }

        outlen = m_len;

        p = HMAC(md, key->u.s.raw.start, key->u.s.raw.length, data.start,
                 data.length, dst, &m_len);

        if (njs_slow_path(p == NULL || m_len != outlen)) {
            njs_webcrypto_error(vm, "HMAC() failed");
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
        mctx = njs_evp_md_ctx_new();
        if (njs_slow_path(mctx == NULL)) {
            njs_webcrypto_error(vm, "njs_evp_md_ctx_new() failed");
            goto fail;
        }

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

        olen = EVP_PKEY_size(key->u.a.pkey);
        dst = njs_mp_zalloc(njs_vm_memory_pool(vm), olen);
        if (njs_slow_path(dst == NULL)) {
            njs_vm_memory_error(vm);
            goto fail;
        }

        pctx = EVP_PKEY_CTX_new(key->u.a.pkey, NULL);
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

        ret = njs_set_rsa_padding(vm, options, key->u.a.pkey, pctx, alg->type);
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

            if (alg->type == NJS_ALGORITHM_ECDSA) {
                ret = njs_convert_der_to_p1363(vm, key->u.a.pkey, dst, outlen,
                                               &dst, &outlen);
                if (njs_slow_path(ret != NJS_OK)) {
                    goto fail;
                }
            }

        } else {
            if (alg->type == NJS_ALGORITHM_ECDSA) {
                ret = njs_convert_p1363_to_der(vm, key->u.a.pkey, sig.start,
                                               sig.length, &sig.start,
                                               &sig.length);
                if (njs_slow_path(ret != NJS_OK)) {
                    goto fail;
                }
            }

            ret = EVP_PKEY_verify(pctx, sig.start, sig.length, m, m_len);
            if (njs_slow_path(ret < 0)) {
                njs_webcrypto_error(vm, "EVP_PKEY_verify() failed");
                goto fail;
            }
        }

        njs_evp_md_ctx_free(mctx);

        EVP_PKEY_CTX_free(pctx);

        break;
    }

    if (!verify) {
        ret = njs_vm_value_array_buffer_set(vm, njs_value_arg(&result), dst,
                                            outlen);
        if (njs_slow_path(ret != NJS_OK)) {
            goto fail;
        }

    } else {
        njs_value_boolean_set(njs_value_arg(&result), ret != 0);
    }

    return njs_webcrypto_result(vm, &result, NJS_OK, retval);

fail:

    if (mctx != NULL) {
        njs_evp_md_ctx_free(mctx);
    }

    if (pctx != NULL) {
        EVP_PKEY_CTX_free(pctx);
    }

    return njs_webcrypto_result(vm, NULL, NJS_ERROR, retval);
}


static njs_int_t
njs_ext_unwrap_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_vm_internal_error(vm, "\"unwrapKey\" not implemented");
    return NJS_ERROR;
}


static njs_int_t
njs_ext_wrap_key(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_vm_internal_error(vm, "\"wrapKey\" not implemented");
    return NJS_ERROR;
}


static njs_int_t
njs_key_ext_algorithm(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    u_char               *start;
    njs_int_t            ret;
    njs_str_t            *name;
    const BIGNUM         *n_bn, *e_bn;
    const EC_GROUP       *group;
    njs_opaque_value_t   alg, name_s, val, hash;
    njs_webcrypto_key_t  *key;

    static const njs_str_t  string_pexponent = njs_str("publicExponent");

    key = njs_vm_external(vm, njs_webcrypto_crypto_key_proto_id, value);
    if (njs_slow_path(key == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    name = &njs_webcrypto_alg[key->alg->type].name;
    ret = njs_vm_value_string_create(vm, njs_value_arg(&alg), name->start,
                                     name->length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    (void) njs_vm_value_string_create(vm, njs_value_arg(&name_s),
                                     (u_char *) "name", njs_length("name"));

    ret = njs_vm_object_alloc(vm, retval, &name_s, &alg, NULL);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    switch (key->alg->type) {
    case NJS_ALGORITHM_RSASSA_PKCS1_v1_5:
    case NJS_ALGORITHM_RSA_PSS:
    case NJS_ALGORITHM_RSA_OAEP:
        /* RsaHashedKeyGenParams */

        njs_assert(key->u.a.pkey != NULL);
        njs_assert(EVP_PKEY_id(key->u.a.pkey) == EVP_PKEY_RSA);

        njs_rsa_get0_key(njs_pkey_get_rsa_key(key->u.a.pkey), &n_bn, &e_bn,
                         NULL);

        njs_value_number_set(njs_value_arg(&val), BN_num_bits(n_bn));

        ret = njs_vm_object_prop_set(vm, retval, &string_ml, &val);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        start = njs_mp_alloc(njs_vm_memory_pool(vm), BN_num_bytes(e_bn));
        if (njs_slow_path(start == NULL)) {
            njs_vm_memory_error(vm);
            return NJS_ERROR;
        }

        BN_bn2bin(e_bn, start);

        ret = njs_vm_value_buffer_set(vm, njs_value_arg(&val), start,
                                      BN_num_bytes(e_bn));
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_vm_object_prop_set(vm, retval, &string_pexponent, &val);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        name = njs_algorithm_hash_name(key->hash);
        ret = njs_vm_value_string_create(vm, njs_value_arg(&hash), name->start,
                                         name->length);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_vm_object_alloc(vm, njs_value_arg(&val), NULL);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_vm_object_prop_set(vm, njs_value_arg(&val), &string_name,
                                     &hash);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_vm_object_prop_set(vm, retval, &string_hash, &val);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        break;

    case NJS_ALGORITHM_AES_GCM:
    case NJS_ALGORITHM_AES_CTR:
    case NJS_ALGORITHM_AES_CBC:
        /* AesKeyGenParams */

        njs_value_number_set(njs_value_arg(&val), key->u.s.raw.length * 8);

        ret = njs_vm_object_prop_set(vm, retval, &string_length, &val);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        break;

    case NJS_ALGORITHM_ECDSA:
    case NJS_ALGORITHM_ECDH:
        /* EcKeyGenParams */

        njs_assert(key->u.a.pkey != NULL);
        njs_assert(EVP_PKEY_id(key->u.a.pkey) == EVP_PKEY_EC);

        group = EC_KEY_get0_group(njs_pkey_get_ec_key(key->u.a.pkey));

        name = njs_algorithm_curve_name(EC_GROUP_get_curve_name(group));

        ret = njs_vm_value_string_create(vm, njs_value_arg(&val), name->start,
                                         name->length);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_vm_object_prop_set(vm, retval, &string_curve, &val);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        break;

    case NJS_ALGORITHM_HMAC:
    default:
        /* HmacKeyGenParams */

        name = njs_algorithm_hash_name(key->hash);
        ret = njs_vm_value_string_create(vm, njs_value_arg(&val), name->start,
                                         name->length);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        ret = njs_vm_object_prop_set(vm, retval, &string_hash, &val);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        break;
    }

    return NJS_OK;
}


static njs_int_t
njs_key_ext_extractable(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_webcrypto_key_t  *key;

    key = njs_vm_external(vm, njs_webcrypto_crypto_key_proto_id, value);
    if (njs_slow_path(key == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    njs_value_boolean_set(retval, key->extractable);

    return NJS_OK;
}


static njs_int_t
njs_key_ext_type(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    const char           *type;
    njs_webcrypto_key_t  *key;

    key = njs_vm_external(vm, njs_webcrypto_crypto_key_proto_id, value);
    if (njs_slow_path(key == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    if (key->alg->raw) {
        (void) njs_vm_value_string_create(vm, retval, (u_char *) "secret",
                                          njs_length("secret"));
    } else {
        type = key->u.a.privat ? "private": "public";
        (void) njs_vm_value_string_create(vm, retval, (u_char *) type,
                                          key->u.a.privat ? 7 : 6);
    }

    return NJS_OK;
}


static njs_int_t
njs_key_ext_usages(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_webcrypto_key_t  *key;

    key = njs_vm_external(vm, njs_webcrypto_crypto_key_proto_id, value);
    if (njs_slow_path(key == NULL)) {
        njs_value_undefined_set(retval);
        return NJS_DECLINED;
    }

    return njs_key_ops(vm, retval, key->usage);
}


static njs_int_t
njs_ext_get_random_values(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused, njs_value_t *retval)
{
    njs_int_t    ret;
    njs_str_t    fill;
    njs_value_t  *buffer;

    buffer = njs_arg(args, nargs, 1);

    ret = njs_vm_value_to_bytes(vm, &fill, buffer);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    if (njs_slow_path(fill.length > 65536)) {
        njs_vm_type_error(vm, "requested length exceeds 65536 bytes");
        return NJS_ERROR;
    }

    if (RAND_bytes(fill.start, fill.length) != 1) {
        njs_webcrypto_error(vm, "RAND_bytes() failed");
        return NJS_ERROR;
    }

    njs_value_assign(retval, buffer);

    return NJS_OK;
}


static void
njs_webcrypto_cleanup_pkey(void *data)
{
    njs_webcrypto_key_t  *key = data;

    if (!key->alg->raw) {
        EVP_PKEY_free(key->u.a.pkey);
    }
}


static njs_webcrypto_key_t *
njs_webcrypto_key_alloc(njs_vm_t *vm, njs_webcrypto_algorithm_t *alg,
    unsigned usage, njs_bool_t extractable)
{
    njs_mp_cleanup_t     *cln;
    njs_webcrypto_key_t  *key;

    key = njs_mp_zalloc(njs_vm_memory_pool(vm), sizeof(njs_webcrypto_key_t));
    if (njs_slow_path(key == NULL)) {
        njs_vm_memory_error(vm);
        return NULL;
    }

    cln = njs_mp_cleanup_add(njs_vm_memory_pool(vm), 0);
    if (cln == NULL) {
        njs_vm_memory_error(vm);
        return NULL;
    }

    cln->handler = njs_webcrypto_cleanup_pkey;
    cln->data = key;

    key->alg = alg;
    key->usage = usage;
    key->extractable = extractable;

    return key;
}


static njs_webcrypto_key_format_t
njs_key_format(njs_vm_t *vm, njs_value_t *value)
{
    njs_int_t              ret;
    njs_str_t              format;
    njs_opaque_value_t     string;
    njs_webcrypto_entry_t  *e;

    ret = njs_value_to_string(vm, njs_value_arg(&string), value);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_KEY_FORMAT_UNKNOWN;
    }

    njs_value_string_get(njs_value_arg(&string), &format);

    for (e = &njs_webcrypto_format[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&format, &e->name)) {
            return e->value;
        }
    }

    njs_vm_type_error(vm, "unknown key format: \"%V\"", &format);

    return NJS_KEY_FORMAT_UNKNOWN;
}


static njs_str_t *
njs_format_string(njs_webcrypto_key_format_t fmt)
{
    njs_webcrypto_entry_t  *e;

    for (e = &njs_webcrypto_format[0]; e->name.length != 0; e++) {
        if (fmt == e->value) {
            break;
        }
    }

    return &e->name;
}


static njs_int_t
njs_key_usage_array_handler(njs_vm_t *vm, njs_iterator_args_t *args,
    njs_value_t *value, int64_t index, njs_value_t *retval)
{
    unsigned               *mask;
    njs_str_t              u;
    njs_int_t              ret;
    njs_opaque_value_t     usage;
    njs_webcrypto_entry_t  *e;

    njs_value_assign(&usage, value);

    ret = njs_value_to_string(vm, njs_value_arg(&usage), njs_value_arg(&usage));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_value_string_get(njs_value_arg(&usage), &u);

    for (e = &njs_webcrypto_usage[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&u, &e->name)) {
            mask = args->data;
            *mask |= e->value;
            return NJS_OK;
        }
    }

    njs_vm_type_error(vm, "unknown key usage: \"%V\"", &u);

    return NJS_ERROR;
}


static njs_int_t
njs_key_usage(njs_vm_t *vm, njs_value_t *value, unsigned *mask)
{
    int64_t              length;
    njs_int_t            ret;
    njs_opaque_value_t   retval;
    njs_iterator_args_t  args;

    if (!njs_value_is_array(value)) {
        njs_vm_type_error(vm, "\"keyUsages\" argument must be an Array");
        return NJS_ERROR;
    }

    ret = njs_vm_array_length(vm, value, &length);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    *mask = 0;

    njs_value_assign(&args.value, value);
    args.from = 0;
    args.to = length;
    args.data = mask;

    return njs_vm_object_iterate(vm, &args, njs_key_usage_array_handler,
                                 njs_value_arg(&retval));
}


static njs_int_t
njs_key_ops(njs_vm_t *vm, njs_value_t *retval, unsigned mask)
{
    njs_int_t              ret;
    njs_value_t            *value;
    njs_webcrypto_entry_t  *e;

    ret = njs_vm_array_alloc(vm, retval, 4);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    for (e = &njs_webcrypto_usage[0]; e->name.length != 0; e++) {
        if (mask & e->value) {
            value = njs_vm_array_push(vm, retval);
            if (value == NULL) {
                return NJS_ERROR;
            }

            ret = njs_vm_value_string_create(vm, value, e->name.start,
                                             e->name.length);
            if (ret != NJS_OK) {
                return NJS_ERROR;
            }
        }
    }

    return NJS_OK;
}


static njs_webcrypto_algorithm_t *
njs_key_algorithm(njs_vm_t *vm, njs_value_t *options)
{
    njs_int_t                  ret;
    njs_str_t                  a;
    njs_value_t                *val;
    njs_opaque_value_t         name;
    njs_webcrypto_entry_t      *e;
    njs_webcrypto_algorithm_t  *alg;

    if (njs_value_is_object(options)) {
        val = njs_vm_object_prop(vm, options, &string_name, &name);
        if (njs_slow_path(val == NULL)) {
            njs_vm_type_error(vm, "algorithm name is not provided");
            return NULL;
        }

    } else {
        njs_value_assign(&name, options);
    }

    ret = njs_value_to_string(vm, njs_value_arg(&name), njs_value_arg(&name));
    if (njs_slow_path(ret != NJS_OK)) {
        return NULL;
    }

    njs_value_string_get(njs_value_arg(&name), &a);

    for (e = &njs_webcrypto_alg[0]; e->name.length != 0; e++) {
        if (njs_strstr_case_eq(&a, &e->name)) {
            alg = (njs_webcrypto_algorithm_t *) e->value;
            if (alg->usage & NJS_KEY_USAGE_UNSUPPORTED) {
                njs_vm_type_error(vm, "unsupported algorithm: \"%V\"", &a);
                return NULL;
            }

            return alg;
        }
    }

    njs_vm_type_error(vm, "unknown algorithm name: \"%V\"", &a);

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
    njs_value_t            *val;
    njs_opaque_value_t     value;
    njs_webcrypto_entry_t  *e;

    if (njs_value_is_object(options)) {
        val = njs_vm_object_prop(vm, options, &string_hash, &value);
        if (njs_slow_path(val == NULL)) {
            njs_value_undefined_set(njs_value_arg(&value));
        }

    } else {
        njs_value_assign(&value, options);
    }

    ret = njs_value_to_string(vm, njs_value_arg(&value), njs_value_arg(&value));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_value_string_get(njs_value_arg(&value), &name);

    for (e = &njs_webcrypto_hash[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&name, &e->name)) {
            *hash = e->value;
            return NJS_OK;
        }
    }

    njs_vm_type_error(vm, "unknown hash name: \"%V\"", &name);

    return NJS_ERROR;
}


static njs_str_t *
njs_algorithm_hash_name(njs_webcrypto_hash_t hash)
{
    njs_webcrypto_entry_t  *e;

    for (e = &njs_webcrypto_hash[0]; e->name.length != 0; e++) {
        if (e->value == hash) {
            return &e->name;
        }
    }

    return &e->name;
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
njs_algorithm_curve(njs_vm_t *vm, njs_value_t *options, int *curve)
{
    njs_int_t              ret;
    njs_str_t              name;
    njs_value_t            *val;
    njs_opaque_value_t     value;
    njs_webcrypto_entry_t  *e;

    if (*curve != 0) {
        return NJS_OK;
    }

    val = njs_vm_object_prop(vm, options, &string_curve, &value);
    if (njs_slow_path(val == NULL)) {
        njs_value_undefined_set(njs_value_arg(&value));
    }

    ret = njs_value_to_string(vm, njs_value_arg(&value), njs_value_arg(&value));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    njs_value_string_get(njs_value_arg(&value), &name);

    for (e = &njs_webcrypto_curve[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&name, &e->name)) {
            *curve = e->value;
            return NJS_OK;
        }
    }

    njs_vm_type_error(vm, "unknown namedCurve: \"%V\"", &name);

    return NJS_ERROR;
}


static njs_str_t *
njs_algorithm_curve_name(int curve)
{
    njs_webcrypto_entry_t  *e;

    for (e = &njs_webcrypto_curve[0]; e->name.length != 0; e++) {
        if (e->value == (uintptr_t) curve) {
            return &e->name;
        }
    }

    return &e->name;
}


static njs_int_t
njs_promise_trampoline(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval)
{
    njs_function_t  *callback;

    callback = njs_value_function(njs_argument(args, 1));

    if (callback != NULL) {
        return njs_vm_invoke(vm, callback, njs_argument(args, 2), 1, retval);
    }

    return NJS_OK;
}


static njs_int_t
njs_webcrypto_result(njs_vm_t *vm, njs_opaque_value_t *result, njs_int_t rc,
    njs_value_t *retval)
{
    njs_int_t           ret;
    njs_function_t      *callback;
    njs_opaque_value_t  promise, arguments[2];

    ret = njs_vm_promise_create(vm, njs_value_arg(&promise),
                                njs_value_arg(&arguments));
    if (ret != NJS_OK) {
        goto error;
    }

    callback = njs_vm_function_alloc(vm, njs_promise_trampoline, 0, 0);
    if (callback == NULL) {
        goto error;
    }

    njs_value_assign(&arguments[0], &arguments[(rc != NJS_OK)]);

    if (rc != NJS_OK) {
        njs_vm_exception_get(vm, njs_value_arg(&arguments[1]));

    } else {
        njs_value_assign(&arguments[1], result);
    }

    ret = njs_vm_enqueue_job(vm, callback, njs_value_arg(&arguments), 2);
    if (ret == NJS_ERROR) {
        goto error;
    }

    njs_value_assign(retval, &promise);

    return NJS_OK;

error:

    njs_vm_internal_error(vm, "cannot make webcrypto result");

    return NJS_ERROR;
}


static njs_int_t
njs_webcrypto_array_buffer(njs_vm_t *vm, njs_value_t *retval,
    u_char *start, size_t length)
{
    u_char  *dst;

    dst = njs_mp_alloc(njs_vm_memory_pool(vm), length);
    if (njs_slow_path(dst == NULL)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    memcpy(dst, start, length);

    return njs_vm_value_array_buffer_set(vm, retval, dst, length);
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

    njs_vm_error(vm, "%*s", p - errstr, errstr);
}


static njs_int_t
njs_webcrypto_init(njs_vm_t *vm)
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
