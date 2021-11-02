
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


static njs_hash_alg_t *njs_crypto_algorithm(njs_vm_t *vm,
    const njs_value_t *value);
static njs_crypto_enc_t *njs_crypto_encoding(njs_vm_t *vm,
    const njs_value_t *value);
static njs_int_t njs_buffer_digest(njs_vm_t *vm, njs_value_t *value,
    const njs_str_t *src);


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
     njs_str("buffer"),
     njs_buffer_digest
   },

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


static njs_int_t
njs_crypto_create_hash(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_digest_t        *dgst;
    njs_hash_alg_t      *alg;
    njs_object_value_t  *hash;

    alg = njs_crypto_algorithm(vm, njs_arg(args, nargs, 1));
    if (njs_slow_path(alg == NULL)) {
        return NJS_ERROR;
    }

    hash = njs_object_value_alloc(vm, NJS_OBJ_TYPE_CRYPTO_HASH, 0, NULL);
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

    njs_set_data(&hash->value, dgst, NJS_DATA_TAG_CRYPTO_HASH);
    njs_set_object_value(&vm->retval, hash);

    return NJS_OK;
}


static njs_int_t
njs_hash_prototype_update(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t tag)
{
    njs_str_t                    data;
    njs_int_t                    ret;
    njs_hmac_t                   *ctx;
    njs_value_t                  *this, dst;
    njs_digest_t                 *dgst;
    njs_typed_array_t            *array;
    const njs_value_t            *value;
    njs_array_buffer_t           *buffer;
    const njs_buffer_encoding_t  *encoding;

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_object_data(this, tag))) {
        njs_type_error(vm, "\"this\" is not a hash object");
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 1);

    switch (value->type) {
    case NJS_STRING:
        encoding = njs_buffer_encoding(vm, njs_arg(args, nargs, 2));
        if (njs_slow_path(encoding == NULL)) {
            return NJS_ERROR;
        }

        ret = njs_buffer_decode_string(vm, value, &dst, encoding);
        if (njs_slow_path(ret != NJS_OK)) {
            return NJS_ERROR;
        }

        njs_string_get(&dst, &data);
        break;

    case NJS_TYPED_ARRAY:
    case NJS_DATA_VIEW:
        array = njs_typed_array(value);
        buffer = array->buffer;
        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        data.start = &buffer->u.u8[array->offset];
        data.length = array->byte_length;
        break;

    default:
        njs_type_error(vm, "data argument \"%s\" is not a string "
                       "or Buffer-like object", njs_type_string(value->type));

        return NJS_ERROR;
    }

    if (tag == NJS_DATA_TAG_CRYPTO_HASH) {
        dgst = njs_object_data(this);
        if (njs_slow_path(dgst->alg == NULL)) {
            njs_error(vm, "Digest already called");
            return NJS_ERROR;
        }

        dgst->alg->update(&dgst->u, data.start, data.length);

    } else {
        ctx = njs_object_data(this);
        if (njs_slow_path(ctx->alg == NULL)) {
            njs_error(vm, "Digest already called");
            return NJS_ERROR;
        }

        ctx->alg->update(&ctx->u, data.start, data.length);
    }

    vm->retval = *this;

    return NJS_OK;
}


static njs_int_t
njs_hash_prototype_digest(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t tag)
{
    njs_str_t         str;
    njs_hmac_t        *ctx;
    njs_value_t       *this;
    njs_digest_t      *dgst;
    njs_hash_alg_t    *alg;
    njs_crypto_enc_t  *enc;
    u_char            hash1[32], digest[32];

    this = njs_argument(args, 0);
    if (njs_slow_path(!njs_is_object_data(this, tag))) {
        njs_type_error(vm, "\"this\" is not a hash object");
        return NJS_ERROR;
    }

    enc = njs_crypto_encoding(vm, njs_arg(args, nargs, 1));
    if (njs_slow_path(enc == NULL)) {
        return NJS_ERROR;
    }

    if (tag == NJS_DATA_TAG_CRYPTO_HASH) {
        dgst = njs_object_data(this);
        if (njs_slow_path(dgst->alg == NULL)) {
            goto exception;
        }

        alg = dgst->alg;
        alg->final(digest, &dgst->u);
        dgst->alg = NULL;

    } else {
        ctx = njs_object_data(this);
        if (njs_slow_path(ctx->alg == NULL)) {
            goto exception;
        }

        alg = ctx->alg;
        alg->final(hash1, &ctx->u);

        alg->init(&ctx->u);
        alg->update(&ctx->u, ctx->opad, 64);
        alg->update(&ctx->u, hash1, alg->size);
        alg->final(digest, &ctx->u);
        ctx->alg = NULL;
    }

    str.start = digest;
    str.length = alg->size;

    return enc->encode(vm, &vm->retval, &str);

exception:

    njs_error(vm, "Digest already called");
    return NJS_ERROR;
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
        .value = njs_native_function2(njs_hash_prototype_update, 0,
                                      NJS_DATA_TAG_CRYPTO_HASH),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("digest"),
        .value = njs_native_function2(njs_hash_prototype_digest, 0,
                                      NJS_DATA_TAG_CRYPTO_HASH),
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
    njs_str_t           key;
    njs_uint_t          i;
    njs_hmac_t          *ctx;
    njs_hash_alg_t      *alg;
    njs_typed_array_t   *array;
    const njs_value_t   *value;
    njs_array_buffer_t  *buffer;
    njs_object_value_t  *hmac;
    u_char              digest[32], key_buf[64];

    alg = njs_crypto_algorithm(vm, njs_arg(args, nargs, 1));
    if (njs_slow_path(alg == NULL)) {
        return NJS_ERROR;
    }

    value = njs_arg(args, nargs, 2);

    switch (value->type) {
    case NJS_STRING:
        njs_string_get(value, &key);
        break;

    case NJS_TYPED_ARRAY:
    case NJS_DATA_VIEW:
        array = njs_typed_array(value);
        buffer = array->buffer;
        if (njs_slow_path(njs_is_detached_buffer(buffer))) {
            njs_type_error(vm, "detached buffer");
            return NJS_ERROR;
        }

        key.start = &buffer->u.u8[array->offset];
        key.length = array->byte_length;
        break;

    default:
        njs_type_error(vm, "key argument \"%s\" is not a string "
                       "or Buffer-like object", njs_type_string(value->type));

        return NJS_ERROR;
    }

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

    hmac = njs_object_value_alloc(vm, NJS_OBJ_TYPE_CRYPTO_HMAC, 0, NULL);
    if (njs_slow_path(hmac == NULL)) {
        return NJS_ERROR;
    }

    njs_set_data(&hmac->value, ctx, NJS_DATA_TAG_CRYPTO_HMAC);
    njs_set_object_value(&vm->retval, hmac);

    return NJS_OK;
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
        .value = njs_native_function2(njs_hash_prototype_update, 0,
                                      NJS_DATA_TAG_CRYPTO_HMAC),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("digest"),
        .value = njs_native_function2(njs_hash_prototype_digest, 0,
                                      NJS_DATA_TAG_CRYPTO_HMAC),
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
njs_crypto_algorithm(njs_vm_t *vm, const njs_value_t *value)
{
    njs_str_t       name;
    njs_hash_alg_t  *e;

    if (njs_slow_path(!njs_is_string(value))) {
        njs_type_error(vm, "algorithm must be a string");
        return NULL;
    }

    njs_string_get(value, &name);

    for (e = &njs_hash_algorithms[0]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&name, &e->name)) {
            return e;
        }
    }

    njs_type_error(vm, "not supported algorithm: \"%V\"", &name);

    return NULL;
}


static njs_crypto_enc_t *
njs_crypto_encoding(njs_vm_t *vm, const njs_value_t *value)
{
    njs_str_t         name;
    njs_crypto_enc_t  *e;

    if (njs_slow_path(!njs_is_string(value))) {
        if (njs_is_defined(value)) {
            njs_type_error(vm, "encoding must be a string");
            return NULL;
        }

        return &njs_encodings[0];
    }

    njs_string_get(value, &name);

    for (e = &njs_encodings[1]; e->name.length != 0; e++) {
        if (njs_strstr_eq(&name, &e->name)) {
            return e;
        }
    }

    njs_type_error(vm, "Unknown digest encoding: \"%V\"", &name);

    return NULL;
}


static njs_int_t
njs_buffer_digest(njs_vm_t *vm, njs_value_t *value, const njs_str_t *src)
{
    return njs_buffer_new(vm, value, src->start, src->length);
}
