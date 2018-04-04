
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <nxt_md5.h>
#include <nxt_sha1.h>
#include <nxt_sha2.h>
#include <njs_crypto.h>
#include <stdio.h>
#include <string.h>
#include <math.h>


typedef void (*njs_hash_init)(void *ctx);
typedef void (*njs_hash_update)(void *ctx, const void *data, size_t size);
typedef void (*njs_hash_final)(u_char *result, void *ctx);

typedef njs_ret_t (*njs_digest_encode)(njs_vm_t *vm, njs_value_t *value,
    const nxt_str_t *src);


typedef struct {
    nxt_str_t           name;

    size_t              size;
    njs_hash_init       init;
    njs_hash_update     update;
    njs_hash_final      final;
} njs_hash_alg_t;

typedef struct {
    union {
        nxt_md5_t       md5;
        nxt_sha1_t      sha1;
        nxt_sha2_t      sha2;
    } u;

    njs_hash_alg_t      *alg;
} njs_digest_t;

typedef struct {
    nxt_str_t           key;
    u_char              opad[64];

    union {
        nxt_md5_t       md5;
        nxt_sha1_t      sha1;
        nxt_sha2_t      sha2;
    } u;

    njs_hash_alg_t      *alg;
} njs_hmac_t;


typedef struct {
    nxt_str_t             name;

    njs_digest_encode     encode;
} njs_crypto_enc_t;


static njs_hash_alg_t njs_hash_algorithms[] = {

   {
     nxt_string("md5"),
     16,
     (njs_hash_init) nxt_md5_init,
     (njs_hash_update) nxt_md5_update,
     (njs_hash_final) nxt_md5_final
   },

   {
     nxt_string("sha1"),
     20,
     (njs_hash_init) nxt_sha1_init,
     (njs_hash_update) nxt_sha1_update,
     (njs_hash_final) nxt_sha1_final
   },

   {
     nxt_string("sha256"),
     32,
     (njs_hash_init) nxt_sha2_init,
     (njs_hash_update) nxt_sha2_update,
     (njs_hash_final) nxt_sha2_final
   },

   {
    nxt_null_string,
    0,
    NULL,
    NULL,
    NULL
   }

};

static njs_crypto_enc_t njs_encodings[] = {

   {
     nxt_string("hex"),
     njs_string_hex
   },

   {
     nxt_string("base64"),
     njs_string_base64
   },

   {
     nxt_string("base64url"),
     njs_string_base64url
   },

   {
    nxt_null_string,
    NULL
   }
};


static njs_hash_alg_t *njs_crypto_alg(njs_vm_t *vm, const nxt_str_t *name);
static njs_crypto_enc_t *njs_crypto_encoding(njs_vm_t *vm,
    const nxt_str_t *name);


static njs_object_value_t *
njs_crypto_object_value_alloc(njs_vm_t *vm, nxt_uint_t proto)
{
    njs_object_value_t  *ov;

    ov = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_object_value_t));

    if (nxt_fast_path(ov != NULL)) {
        nxt_lvlhsh_init(&ov->object.hash);
        nxt_lvlhsh_init(&ov->object.shared_hash);
        ov->object.type = NJS_OBJECT_VALUE;
        ov->object.shared = 0;
        ov->object.extensible = 1;

        ov->object.__proto__ = &vm->prototypes[proto].object;
        return ov;
    }

    njs_memory_error(vm);

    return NULL;
}


static njs_ret_t
njs_crypto_create_hash(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t           alg_name;
    njs_digest_t        *dgst;
    njs_hash_alg_t      *alg;
    njs_object_value_t  *hash;

    if (nxt_slow_path(nargs < 2 || !njs_is_string(&args[1]))) {
        njs_type_error(vm, "algorithm must be a string");
        return NJS_ERROR;
    }

    njs_string_get(&args[1], &alg_name);

    alg = njs_crypto_alg(vm, &alg_name);
    if (nxt_slow_path(alg == NULL)) {
        return NJS_ERROR;
    }

    hash = njs_crypto_object_value_alloc(vm, NJS_PROTOTYPE_CRYPTO_HASH);
    if (nxt_slow_path(hash == NULL)) {
        return NJS_ERROR;
    }

    dgst = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_digest_t));
    if (nxt_slow_path(dgst == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    dgst->alg = alg;

    alg->init(&dgst->u);

    njs_value_data_set(&hash->value, dgst);

    vm->retval.data.u.object_value = hash;
    vm->retval.type = NJS_OBJECT_VALUE;
    vm->retval.data.truth = 1;

    return NJS_OK;
}


static njs_ret_t
njs_hash_prototype_update(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t           data;
    njs_digest_t        *dgst;
    njs_object_value_t  *hash;

    if (nxt_slow_path(nargs < 2 || !njs_is_string(&args[1]))) {
        njs_type_error(vm, "data must be a string");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_object_value(&args[0]))) {
        njs_type_error(vm, "'this' is not an object_value");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_data(&args[0].data.u.object_value->value))) {
        njs_type_error(vm, "value of 'this' is not a data type");
        return NJS_ERROR;
    }

    hash = args[0].data.u.object_value;

    njs_string_get(&args[1], &data);

    dgst = njs_value_data(&hash->value);

    if (nxt_slow_path(dgst->alg == NULL)) {
        njs_error(vm, "Digest already called");
        return NJS_ERROR;
    }

    dgst->alg->update(&dgst->u, data.start, data.length);

    vm->retval = args[0];

    return NJS_OK;
}


static njs_ret_t
njs_hash_prototype_digest(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    u_char              digest[32], *p;
    njs_ret_t           ret;
    nxt_str_t           enc_name, str;
    njs_digest_t        *dgst;
    njs_hash_alg_t      *alg;
    njs_crypto_enc_t    *enc;
    njs_object_value_t  *hash;

    if (nxt_slow_path(nargs > 1 && !njs_is_string(&args[1]))) {
        njs_type_error(vm, "encoding must be a string");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_object_value(&args[0]))) {
        njs_type_error(vm, "'this' is not an object_value");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_data(&args[0].data.u.object_value->value))) {
        njs_type_error(vm, "value of 'this' is not a data type");
        return NJS_ERROR;
    }

    enc = NULL;

    if (nargs > 1) {
        njs_string_get(&args[1], &enc_name);

        enc = njs_crypto_encoding(vm, &enc_name);
        if (nxt_slow_path(enc == NULL)) {
            return NJS_ERROR;
        }
    }

    hash = args[0].data.u.object_value;

    dgst = njs_value_data(&hash->value);

    if (nxt_slow_path(dgst->alg == NULL)) {
        njs_error(vm, "Digest already called");
        return NJS_ERROR;
    }

    alg = dgst->alg;

    alg->final(digest, &dgst->u);

    str.start = digest;
    str.length = alg->size;

    if (enc == NULL) {
        p = njs_string_alloc(vm, &vm->retval, str.length, 0);

        if (nxt_fast_path(p != NULL)) {
            memcpy(p, str.start, str.length);
            ret = NJS_OK;

        } else {
            ret = NJS_ERROR;
        }

    } else {
        ret = enc->encode(vm, &vm->retval, &str);
    }

    dgst->alg = NULL;

    return ret;
}


static njs_ret_t
njs_hash_prototype_to_string(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    static const njs_value_t  string = njs_string("[object Hash]");

    vm->retval = string;

    return NJS_OK;
}


static const njs_object_prop_t  njs_hash_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Hash"),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_hash_prototype_to_string, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("update"),
        .value = njs_native_function(njs_hash_prototype_update, 0,
                                     NJS_OBJECT_ARG, NJS_SKIP_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("digest"),
        .value = njs_native_function(njs_hash_prototype_digest, 0,
                                     NJS_OBJECT_ARG, NJS_SKIP_ARG),
    },
};


const njs_object_init_t  njs_hash_prototype_init = {
    nxt_string("Hash"),
    njs_hash_prototype_properties,
    nxt_nitems(njs_hash_prototype_properties),
};


njs_ret_t
njs_hash_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_crypto_create_hash(vm, args, nargs, unused);
}


const njs_object_init_t  njs_hash_constructor_init = {
    nxt_string("Hash"),
    NULL,
    0,
};


static njs_ret_t
njs_crypto_create_hmac(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    u_char              digest[32], key_buf[64];
    nxt_str_t           alg_name, key;
    nxt_uint_t          i;
    njs_hmac_t          *ctx;
    njs_hash_alg_t      *alg;
    njs_object_value_t  *hmac;

    if (nxt_slow_path(nargs < 2 || !njs_is_string(&args[1]))) {
        njs_type_error(vm, "algorithm must be a string");
        return NJS_ERROR;
    }

    if (nxt_slow_path(nargs < 3 || !njs_is_string(&args[2]))) {
        njs_type_error(vm, "key must be a string");
        return NJS_ERROR;
    }

    njs_string_get(&args[1], &alg_name);

    alg = njs_crypto_alg(vm, &alg_name);
    if (nxt_slow_path(alg == NULL)) {
        return NJS_ERROR;
    }

    njs_string_get(&args[2], &key);

    ctx = nxt_mem_cache_alloc(vm->mem_cache_pool, sizeof(njs_hmac_t));
    if (nxt_slow_path(ctx == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    ctx->alg = alg;

    if (key.length > sizeof(key_buf)) {
        alg->init(&ctx->u);
        alg->update(&ctx->u, key.start, key.length);
        alg->final(digest, &ctx->u);

        memcpy(key_buf, digest, alg->size);
        nxt_explicit_memzero(key_buf + alg->size, sizeof(key_buf) - alg->size);

    } else {
        memcpy(key_buf, key.start, key.length);
        nxt_explicit_memzero(key_buf + key.length,
                             sizeof(key_buf) - key.length);
    }

    for (i = 0; i < 64; i++) {
        ctx->opad[i] = key_buf[i] ^ 0x5c;
    }

    for (i = 0; i < 64; i++) {
         key_buf[i] ^= 0x36;
    }

    alg->init(&ctx->u);
    alg->update(&ctx->u, key_buf, 64);

    hmac = njs_crypto_object_value_alloc(vm, NJS_PROTOTYPE_CRYPTO_HMAC);
    if (nxt_slow_path(hmac == NULL)) {
        return NJS_ERROR;
    }

    njs_value_data_set(&hmac->value, ctx);

    vm->retval.data.u.object_value = hmac;
    vm->retval.type = NJS_OBJECT_VALUE;
    vm->retval.data.truth = 1;

    return NJS_OK;
}


static njs_ret_t
njs_hmac_prototype_update(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_str_t           data;
    njs_hmac_t          *ctx;
    njs_object_value_t  *hmac;

    if (nxt_slow_path(nargs < 2 || !njs_is_string(&args[1]))) {
        njs_type_error(vm, "data must be a string");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_object_value(&args[0]))) {
        njs_type_error(vm, "'this' is not an object_value");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_data(&args[0].data.u.object_value->value))) {
        njs_type_error(vm, "value of 'this' is not a data type");
        return NJS_ERROR;
    }

    hmac = args[0].data.u.object_value;

    njs_string_get(&args[1], &data);

    ctx = njs_value_data(&hmac->value);

    if (nxt_slow_path(ctx->alg == NULL)) {
        njs_error(vm, "Digest already called");
        return NJS_ERROR;
    }

    ctx->alg->update(&ctx->u, data.start, data.length);

    vm->retval = args[0];

    return NJS_OK;
}


static njs_ret_t
njs_hmac_prototype_digest(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    u_char              hash1[32], digest[32], *p;
    nxt_str_t           enc_name, str;
    njs_ret_t           ret;
    njs_hmac_t          *ctx;
    njs_hash_alg_t      *alg;
    njs_crypto_enc_t    *enc;
    njs_object_value_t  *hmac;

    if (nxt_slow_path(nargs > 1 && !njs_is_string(&args[1]))) {
        njs_type_error(vm, "encoding must be a string");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_object_value(&args[0]))) {
        njs_type_error(vm, "'this' is not an object_value");
        return NJS_ERROR;
    }

    if (nxt_slow_path(!njs_is_data(&args[0].data.u.object_value->value))) {
        njs_type_error(vm, "value of 'this' is not a data type");
        return NJS_ERROR;
    }

    enc = NULL;

    if (nargs > 1) {
        njs_string_get(&args[1], &enc_name);

        enc = njs_crypto_encoding(vm, &enc_name);
        if (nxt_slow_path(enc == NULL)) {
            return NJS_ERROR;
        }
    }

    hmac = args[0].data.u.object_value;

    ctx = njs_value_data(&hmac->value);

    if (nxt_slow_path(ctx->alg == NULL)) {
        njs_error(vm, "Digest already called");
        return NJS_ERROR;
    }

    alg = ctx->alg;

    alg->final(hash1, &ctx->u);

    alg->init(&ctx->u);
    alg->update(&ctx->u, ctx->opad, 64);
    alg->update(&ctx->u, hash1, alg->size);
    alg->final(digest, &ctx->u);

    str.start = digest;
    str.length = alg->size;

    if (enc == NULL) {
        p = njs_string_alloc(vm, &vm->retval, str.length, 0);

        if (nxt_fast_path(p != NULL)) {
            memcpy(p, str.start, str.length);
            ret = NJS_OK;

        } else {
            ret = NJS_ERROR;
        }

    } else {
        ret = enc->encode(vm, &vm->retval, &str);
    }

    ctx->alg = NULL;

    return ret;
}


static njs_ret_t
njs_hmac_prototype_to_string(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    static const njs_value_t  string = njs_string("[object Hmac]");

    vm->retval = string;

    return NJS_OK;
}


static const njs_object_prop_t  njs_hmac_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Hmac"),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_hmac_prototype_to_string, 0, 0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("update"),
        .value = njs_native_function(njs_hmac_prototype_update, 0,
                                     NJS_OBJECT_ARG, NJS_SKIP_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("digest"),
        .value = njs_native_function(njs_hmac_prototype_digest, 0,
                                     NJS_OBJECT_ARG, NJS_SKIP_ARG),
    },
};


const njs_object_init_t  njs_hmac_prototype_init = {
    nxt_string("Hmac"),
    njs_hmac_prototype_properties,
    nxt_nitems(njs_hmac_prototype_properties),
};


njs_ret_t
njs_hmac_constructor(njs_vm_t *vm, njs_value_t *args,
    nxt_uint_t nargs, njs_index_t unused)
{
    return njs_crypto_create_hmac(vm, args, nargs, unused);
}


const njs_object_init_t  njs_hmac_constructor_init = {
    nxt_string("Hmac"),
    NULL,
    0,
};


static const njs_object_prop_t  njs_crypto_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("crypto"),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sandbox"),
        .value = njs_value(NJS_BOOLEAN, 1, 1.0),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("createHash"),
        .value = njs_native_function(njs_crypto_create_hash, 0,
                                     NJS_SKIP_ARG),
    },

    {
        .type = NJS_METHOD,
        .name = njs_string("createHmac"),
        .value = njs_native_function(njs_crypto_create_hmac, 0,
                                     NJS_SKIP_ARG),
    },

};


const njs_object_init_t  njs_crypto_object_init = {
    nxt_string("crypto"),
    njs_crypto_object_properties,
    nxt_nitems(njs_crypto_object_properties),
};


static njs_hash_alg_t *
njs_crypto_alg(njs_vm_t *vm, const nxt_str_t *name)
{
    njs_hash_alg_t *e;

    for (e = &njs_hash_algorithms[0]; e->name.length != 0; e++) {
        if (nxt_strstr_eq(name, &e->name)) {
            return e;
        }
    }

    njs_type_error(vm, "not supported algorithm: '%.*s'",
                   (int) name->length, name->start);

    return NULL;
}


static njs_crypto_enc_t *
njs_crypto_encoding(njs_vm_t *vm, const nxt_str_t *name)
{
    njs_crypto_enc_t *e;

    for (e = &njs_encodings[0]; e->name.length != 0; e++) {
        if (nxt_strstr_eq(name, &e->name)) {
            return e;
        }
    }

    njs_type_error(vm, "Unknown digest encoding: '%.*s'",
                   (int) name->length, name->start);

    return NULL;
}
