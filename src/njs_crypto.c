
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef void (*njs_hash_init)(void *ctx);
typedef void (*njs_hash_update)(void *ctx, const void *data, size_t size);
typedef void (*njs_hash_final)(u_char *result, void *ctx);

typedef njs_int_t (*njs_digest_encode)(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);


typedef struct {
    njs_str_t           name;

    size_t              size;
    njs_hash_init       init;
    njs_hash_update     update;
    njs_hash_final      final;
} njs_hash_alg_t;

typedef struct {
    union {
        njs_md5_t       md5;
        njs_sha1_t      sha1;
        njs_sha2_t      sha2;
    } u;

    njs_hash_alg_t      *alg;
} njs_digest_t;

typedef struct {
    njs_str_t           key;
    u_char              opad[64];

    union {
        njs_md5_t       md5;
        njs_sha1_t      sha1;
        njs_sha2_t      sha2;
    } u;

    njs_hash_alg_t      *alg;
} njs_hmac_t;


typedef struct {
    njs_str_t             name;

    njs_digest_encode     encode;
} njs_crypto_enc_t;


static njs_hash_alg_t njs_hash_algorithms[] = {

   {
     njs_str("md5"),
     16,
     (njs_hash_init) njs_md5_init,
     (njs_hash_update) njs_md5_update,
     (njs_hash_final) njs_md5_final
   },

   {
     njs_str("sha1"),
     20,
     (njs_hash_init) njs_sha1_init,
     (njs_hash_update) njs_sha1_update,
     (njs_hash_final) njs_sha1_final
   },

   {
     njs_str("sha256"),
     32,
     (njs_hash_init) njs_sha2_init,
     (njs_hash_update) njs_sha2_update,
     (njs_hash_final) njs_sha2_final
   },

   {
    njs_null_str,
    0,
    NULL,
    NULL,
    NULL
   }

};

static njs_crypto_enc_t njs_encodings[] = {

   {
     njs_str("hex"),
     njs_string_hex
   },

   {
     njs_str("base64"),
     njs_string_base64
   },

   {
     njs_str("base64url"),
     njs_string_base64url
   },

   {
    njs_null_str,
    NULL
   }
};


static njs_hash_alg_t *njs_crypto_alg(njs_vm_t *vm, const njs_str_t *name);
static njs_crypto_enc_t *njs_crypto_encoding(njs_vm_t *vm,
    const njs_str_t *name);


static njs_object_value_t *
njs_crypto_object_value_alloc(njs_vm_t *vm, njs_object_type_t type)
{
    njs_object_value_t  *ov;

    ov = njs_mp_alloc(vm->mem_pool, sizeof(njs_object_value_t));

    if (njs_fast_path(ov != NULL)) {
        njs_lvlhsh_init(&ov->object.hash);
        njs_lvlhsh_init(&ov->object.shared_hash);
        ov->object.type = NJS_OBJECT_VALUE;
        ov->object.shared = 0;
        ov->object.extensible = 1;
        ov->object.error_data = 0;
        ov->object.fast_array = 0;

        ov->object.__proto__ = &vm->prototypes[type].object;
        ov->object.slots = NULL;
        return ov;
    }

    njs_memory_error(vm);

    return NULL;
}


static njs_int_t
njs_crypto_create_hash(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t           alg_name;
    njs_digest_t        *dgst;
    njs_hash_alg_t      *alg;
    njs_object_value_t  *hash;

    if (njs_slow_path(nargs < 2 || !njs_is_string(&args[1]))) {
        njs_type_error(vm, "algorithm must be a string");
        return NJS_ERROR;
    }

    njs_string_get(&args[1], &alg_name);

    alg = njs_crypto_alg(vm, &alg_name);
    if (njs_slow_path(alg == NULL)) {
        return NJS_ERROR;
    }

    hash = njs_crypto_object_value_alloc(vm, NJS_OBJ_TYPE_CRYPTO_HASH);
    if (njs_slow_path(hash == NULL)) {
        return NJS_ERROR;
    }

    dgst = njs_mp_alloc(vm->mem_pool, sizeof(njs_digest_t));
    if (njs_slow_path(dgst == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    dgst->alg = alg;

    alg->init(&dgst->u);

    njs_set_data(&hash->value, dgst);
    njs_set_object_value(&vm->retval, hash);

    return NJS_OK;
}


static njs_int_t
njs_hash_prototype_update(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t     data;
    njs_digest_t  *dgst;

    if (njs_slow_path(nargs < 2 || !njs_is_string(&args[1]))) {
        njs_type_error(vm, "data must be a string");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_object_value(&args[0]))) {
        njs_type_error(vm, "\"this\" is not an object_value");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_data(njs_object_value(&args[0])))) {
        njs_type_error(vm, "value of \"this\" is not a data type");
        return NJS_ERROR;
    }

    njs_string_get(&args[1], &data);

    dgst = njs_value_data(njs_object_value(&args[0]));

    if (njs_slow_path(dgst->alg == NULL)) {
        njs_error(vm, "Digest already called");
        return NJS_ERROR;
    }

    dgst->alg->update(&dgst->u, data.start, data.length);

    vm->retval = args[0];

    return NJS_OK;
}


static njs_int_t
njs_hash_prototype_digest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char            digest[32], *p;
    njs_int_t         ret;
    njs_str_t         enc_name, str;
    njs_digest_t      *dgst;
    njs_hash_alg_t    *alg;
    njs_crypto_enc_t  *enc;

    if (njs_slow_path(nargs > 1 && !njs_is_string(&args[1]))) {
        njs_type_error(vm, "encoding must be a string");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_object_value(&args[0]))) {
        njs_type_error(vm, "\"this\" is not an object_value");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_data(njs_object_value(&args[0])))) {
        njs_type_error(vm, "value of \"this\" is not a data type");
        return NJS_ERROR;
    }

    enc = NULL;

    if (nargs > 1) {
        njs_string_get(&args[1], &enc_name);

        enc = njs_crypto_encoding(vm, &enc_name);
        if (njs_slow_path(enc == NULL)) {
            return NJS_ERROR;
        }
    }

    dgst = njs_value_data(njs_object_value(&args[0]));

    if (njs_slow_path(dgst->alg == NULL)) {
        njs_error(vm, "Digest already called");
        return NJS_ERROR;
    }

    alg = dgst->alg;

    alg->final(digest, &dgst->u);

    str.start = digest;
    str.length = alg->size;

    if (enc == NULL) {
        p = njs_string_alloc(vm, &vm->retval, str.length, 0);

        if (njs_fast_path(p != NULL)) {
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


static const njs_object_prop_t  njs_hash_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Hash"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("Hash"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("update"),
        .value = njs_native_function(njs_hash_prototype_update, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("digest"),
        .value = njs_native_function(njs_hash_prototype_digest, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_hash_prototype_init = {
    njs_hash_prototype_properties,
    njs_nitems(njs_hash_prototype_properties),
};


static njs_int_t
njs_hash_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    return njs_crypto_create_hash(vm, args, nargs, unused);
}


static const njs_object_prop_t  njs_hash_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Hash"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 2.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_hash_constructor_init = {
    njs_hash_constructor_properties,
    njs_nitems(njs_hash_constructor_properties),
};


const njs_object_type_init_t  njs_hash_type_init = {
    .constructor = njs_native_ctor(njs_hash_constructor, 2, 0),
    .constructor_props = &njs_hash_constructor_init,
    .prototype_props = &njs_hash_prototype_init,
    .prototype_value = { .object_value = { .value = njs_value(NJS_DATA, 0, 0.0),
                                           .object = { .type = NJS_OBJECT } } },
};


static njs_int_t
njs_crypto_create_hmac(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char              digest[32], key_buf[64];
    njs_str_t           alg_name, key;
    njs_uint_t          i;
    njs_hmac_t          *ctx;
    njs_hash_alg_t      *alg;
    njs_object_value_t  *hmac;

    if (njs_slow_path(nargs < 2 || !njs_is_string(&args[1]))) {
        njs_type_error(vm, "algorithm must be a string");
        return NJS_ERROR;
    }

    if (njs_slow_path(nargs < 3 || !njs_is_string(&args[2]))) {
        njs_type_error(vm, "key must be a string");
        return NJS_ERROR;
    }

    njs_string_get(&args[1], &alg_name);

    alg = njs_crypto_alg(vm, &alg_name);
    if (njs_slow_path(alg == NULL)) {
        return NJS_ERROR;
    }

    njs_string_get(&args[2], &key);

    ctx = njs_mp_alloc(vm->mem_pool, sizeof(njs_hmac_t));
    if (njs_slow_path(ctx == NULL)) {
        njs_memory_error(vm);
        return NJS_ERROR;
    }

    ctx->alg = alg;

    if (key.length > sizeof(key_buf)) {
        alg->init(&ctx->u);
        alg->update(&ctx->u, key.start, key.length);
        alg->final(digest, &ctx->u);

        memcpy(key_buf, digest, alg->size);
        njs_explicit_memzero(key_buf + alg->size, sizeof(key_buf) - alg->size);

    } else {
        memcpy(key_buf, key.start, key.length);
        njs_explicit_memzero(key_buf + key.length,
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

    hmac = njs_crypto_object_value_alloc(vm, NJS_OBJ_TYPE_CRYPTO_HMAC);
    if (njs_slow_path(hmac == NULL)) {
        return NJS_ERROR;
    }

    njs_set_data(&hmac->value, ctx);
    njs_set_object_value(&vm->retval, hmac);

    return NJS_OK;
}


static njs_int_t
njs_hmac_prototype_update(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_str_t   data;
    njs_hmac_t  *ctx;

    if (njs_slow_path(nargs < 2 || !njs_is_string(&args[1]))) {
        njs_type_error(vm, "data must be a string");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_object_value(&args[0]))) {
        njs_type_error(vm, "\"this\" is not an object_value");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_data(njs_object_value(&args[0])))) {
        njs_type_error(vm, "value of \"this\" is not a data type");
        return NJS_ERROR;
    }

    njs_string_get(&args[1], &data);

    ctx = njs_value_data(njs_object_value(&args[0]));

    if (njs_slow_path(ctx->alg == NULL)) {
        njs_error(vm, "Digest already called");
        return NJS_ERROR;
    }

    ctx->alg->update(&ctx->u, data.start, data.length);

    vm->retval = args[0];

    return NJS_OK;
}


static njs_int_t
njs_hmac_prototype_digest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    u_char            hash1[32], digest[32], *p;
    njs_str_t         enc_name, str;
    njs_int_t         ret;
    njs_hmac_t        *ctx;
    njs_hash_alg_t    *alg;
    njs_crypto_enc_t  *enc;

    if (njs_slow_path(nargs > 1 && !njs_is_string(&args[1]))) {
        njs_type_error(vm, "encoding must be a string");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_object_value(&args[0]))) {
        njs_type_error(vm, "\"this\" is not an object_value");
        return NJS_ERROR;
    }

    if (njs_slow_path(!njs_is_data(njs_object_value(&args[0])))) {
        njs_type_error(vm, "value of \"this\" is not a data type");
        return NJS_ERROR;
    }

    enc = NULL;

    if (nargs > 1) {
        njs_string_get(&args[1], &enc_name);

        enc = njs_crypto_encoding(vm, &enc_name);
        if (njs_slow_path(enc == NULL)) {
            return NJS_ERROR;
        }
    }

    ctx = njs_value_data(njs_object_value(&args[0]));

    if (njs_slow_path(ctx->alg == NULL)) {
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

        if (njs_fast_path(p != NULL)) {
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


static const njs_object_prop_t  njs_hmac_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Hmac"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_wellknown_symbol(NJS_SYMBOL_TO_STRING_TAG),
        .value = njs_string("Hmac"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("update"),
        .value = njs_native_function(njs_hmac_prototype_update, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("digest"),
        .value = njs_native_function(njs_hmac_prototype_digest, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_hmac_prototype_init = {
    njs_hmac_prototype_properties,
    njs_nitems(njs_hmac_prototype_properties),
};


static njs_int_t
njs_hmac_constructor(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    return njs_crypto_create_hmac(vm, args, nargs, unused);
}


static const njs_object_prop_t  njs_hmac_constructor_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Hmac"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 3.0),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },
};


const njs_object_init_t  njs_hmac_constructor_init = {
    njs_hmac_constructor_properties,
    njs_nitems(njs_hmac_constructor_properties),
};


static const njs_object_prop_t  njs_crypto_object_properties[] =
{
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("crypto"),
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("sandbox"),
        .value = njs_value(NJS_BOOLEAN, 1, 1.0),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("createHash"),
        .value = njs_native_function(njs_crypto_create_hash, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("createHmac"),
        .value = njs_native_function(njs_crypto_create_hmac, 0),
        .writable = 1,
        .configurable = 1,
    },

};


const njs_object_init_t  njs_crypto_object_init = {
    njs_crypto_object_properties,
    njs_nitems(njs_crypto_object_properties),
};


const njs_object_type_init_t  njs_hmac_type_init = {
    .constructor = njs_native_ctor(njs_hmac_constructor, 3, 0),
    .constructor_props = &njs_hmac_constructor_init,
    .prototype_props = &njs_hmac_prototype_init,
    .prototype_value = { .object_value = { .value = njs_value(NJS_DATA, 0, 0.0),
                                           .object = { .type = NJS_OBJECT } } },
};


static njs_hash_alg_t *
njs_crypto_alg(njs_vm_t *vm, const njs_str_t *name)
{
    njs_hash_alg_t *e;

    for (e = &njs_hash_algorithms[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(name, &e->name)) {
            return e;
        }
    }

    njs_type_error(vm, "not supported algorithm: \"%V\"", name);

    return NULL;
}


static njs_crypto_enc_t *
njs_crypto_encoding(njs_vm_t *vm, const njs_str_t *name)
{
    njs_crypto_enc_t *e;

    for (e = &njs_encodings[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(name, &e->name)) {
            return e;
        }
    }

    njs_type_error(vm, "Unknown digest encoding: \"%V\"", name);

    return NULL;
}
