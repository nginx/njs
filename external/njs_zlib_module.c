/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs.h>
#include <string.h>
#include <zlib.h>

#define NJS_ZLIB_CHUNK_SIZE  1024

static njs_int_t njs_zlib_ext_deflate(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
static njs_int_t njs_zlib_ext_inflate(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused, njs_value_t *retval);
njs_int_t njs_zlib_constant(njs_vm_t *vm, njs_object_prop_t *prop,
    uint32_t unused, njs_value_t *value, njs_value_t *setval,
    njs_value_t *retval);
static njs_int_t njs_zlib_init(njs_vm_t *vm);
static void *njs_zlib_alloc(void *opaque, u_int items, u_int size);
static void njs_zlib_free(void *opaque, void *address);


static njs_external_t  njs_ext_zlib_constants[] = {

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("Z_NO_COMPRESSION"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_zlib_constant,
            .magic32 = Z_NO_COMPRESSION,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("Z_BEST_SPEED"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_zlib_constant,
            .magic32 = Z_BEST_SPEED,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("Z_BEST_COMPRESSION"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_zlib_constant,
            .magic32 = Z_BEST_COMPRESSION,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("Z_FILTERED"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_zlib_constant,
            .magic32 = Z_FILTERED,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("Z_HUFFMAN_ONLY"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_zlib_constant,
            .magic32 = Z_HUFFMAN_ONLY,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("Z_RLE"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_zlib_constant,
            .magic32 = Z_RLE,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("Z_FIXED"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_zlib_constant,
            .magic32 = Z_FIXED,
        }
    },

    {
        .flags = NJS_EXTERN_PROPERTY,
        .name.string = njs_str("Z_DEFAULT_STRATEGY"),
        .enumerable = 1,
        .u.property = {
            .handler = njs_zlib_constant,
            .magic32 = Z_DEFAULT_STRATEGY,
        }
    },

};


static njs_external_t  njs_ext_zlib[] = {

    {
        .flags = NJS_EXTERN_PROPERTY | NJS_EXTERN_SYMBOL,
        .name.symbol = NJS_SYMBOL_TO_STRING_TAG,
        .u.property = {
            .value = "zlib",
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("deflateRawSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_zlib_ext_deflate,
            .magic8 = 1,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("deflateSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_zlib_ext_deflate,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("inflateRawSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_zlib_ext_inflate,
            .magic8 = 1,
        }
    },

    {
        .flags = NJS_EXTERN_METHOD,
        .name.string = njs_str("inflateSync"),
        .writable = 1,
        .configurable = 1,
        .u.method = {
            .native = njs_zlib_ext_inflate,
            .magic8 = 0,
        }
    },

    {
        .flags = NJS_EXTERN_OBJECT,
        .name.string = njs_str("constants"),
        .writable = 1,
        .configurable = 1,
        .u.object = {
            .properties = njs_ext_zlib_constants,
            .nproperties = njs_nitems(njs_ext_zlib_constants),
        }
    },

};


njs_module_t  njs_zlib_module = {
    .name = njs_str("zlib"),
    .preinit = NULL,
    .init = njs_zlib_init,
};


static njs_int_t
njs_zlib_ext_deflate(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t raw, njs_value_t *retval)
{
    int                 rc, level, mem_level, strategy, window_bits;
    u_char              *buffer;
    size_t              chunk_size;
    ssize_t             size;
    njs_chb_t           chain;
    z_stream            stream;
    njs_int_t           ret;
    njs_str_t           data, dictionary;
    njs_value_t         *options, *value;
    njs_opaque_value_t  lvalue;

    static const njs_str_t chunk_size_key = njs_str("chunkSize");
    static const njs_str_t dict_key = njs_str("dictionary");
    static const njs_str_t level_key = njs_str("level");
    static const njs_str_t mem_level_key = njs_str("memLevel");
    static const njs_str_t strategy_key = njs_str("strategy");
    static const njs_str_t window_bits_key = njs_str("windowBits");

    ret = njs_vm_value_to_bytes(vm, &data, njs_arg(args, nargs, 1));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    chunk_size = NJS_ZLIB_CHUNK_SIZE;
    dictionary.start = NULL;
    mem_level = 8;
    level = Z_DEFAULT_COMPRESSION;
    strategy = Z_DEFAULT_STRATEGY;
    window_bits = raw ? -MAX_WBITS : MAX_WBITS;

    options = njs_arg(args, nargs, 2);

    if (njs_value_is_object(options)) {
        value = njs_vm_object_prop(vm, options, &chunk_size_key, &lvalue);
        if (value != NULL) {
            chunk_size = njs_value_number(value);

            if (njs_slow_path(chunk_size < 64)) {
                njs_vm_range_error(vm, "chunkSize must be >= 64");
                return NJS_ERROR;
            }
        }

        value = njs_vm_object_prop(vm, options, &level_key, &lvalue);
        if (value != NULL) {
            level = njs_value_number(value);

            if (njs_slow_path(level < Z_DEFAULT_COMPRESSION
                              || level > Z_BEST_COMPRESSION))
            {
                njs_vm_range_error(vm, "level must be in the range %d..%d",
                                   Z_DEFAULT_COMPRESSION, Z_BEST_COMPRESSION);
                return NJS_ERROR;
            }
        }

        value = njs_vm_object_prop(vm, options, &window_bits_key, &lvalue);
        if (value != NULL) {
            window_bits = njs_value_number(value);

            if (raw) {
                if (njs_slow_path(window_bits < -15 || window_bits > -9)) {
                    njs_vm_range_error(vm, "windowBits must be in the range "
                                       "-15..-9");
                    return NJS_ERROR;
                }

            } else {
                if (njs_slow_path(window_bits < 9 || window_bits > 15)) {
                    njs_vm_range_error(vm, "windowBits must be in the range "
                                       "9..15");
                    return NJS_ERROR;
                }
            }
        }

        value = njs_vm_object_prop(vm, options, &mem_level_key, &lvalue);
        if (value != NULL) {
            mem_level = njs_value_number(value);

            if (njs_slow_path(mem_level < 1 || mem_level > 9)) {
                njs_vm_range_error(vm, "memLevel must be in the range 0..9");
                return NJS_ERROR;
            }
        }

        value = njs_vm_object_prop(vm, options, &strategy_key, &lvalue);
        if (value != NULL) {
            strategy = njs_value_number(value);

            switch (strategy) {
            case Z_FILTERED:
            case Z_HUFFMAN_ONLY:
            case Z_RLE:
            case Z_FIXED:
            case Z_DEFAULT_STRATEGY:
                break;

            default:
                njs_vm_type_error(vm, "unknown strategy: %d", strategy);
                return NJS_ERROR;
            }
        }

        value = njs_vm_object_prop(vm, options, &dict_key, &lvalue);
        if (value != NULL) {
            ret = njs_vm_value_to_bytes(vm, &dictionary, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }
        }
    }

    stream.next_in = data.start;
    stream.avail_in = data.length;

    stream.zalloc = njs_zlib_alloc;
    stream.zfree = njs_zlib_free;
    stream.opaque = njs_vm_memory_pool(vm);

    rc = deflateInit2(&stream, level, Z_DEFLATED, window_bits, mem_level,
                      strategy);
    if (njs_slow_path(rc != Z_OK)) {
        njs_vm_internal_error(vm, "deflateInit2() failed");
        return NJS_ERROR;
    }

    if (dictionary.start != NULL) {
        rc = deflateSetDictionary(&stream, dictionary.start, dictionary.length);
        if (njs_slow_path(rc != Z_OK)) {
            njs_vm_internal_error(vm, "deflateSetDictionary() failed");
            return NJS_ERROR;
        }
    }

    NJS_CHB_MP_INIT(&chain, njs_vm_memory_pool(vm));

    do {
        stream.next_out = njs_chb_reserve(&chain, chunk_size);
        if (njs_slow_path(stream.next_out == NULL)) {
            njs_vm_memory_error(vm);
            goto fail;
        }

        stream.avail_out = chunk_size;

        rc = deflate(&stream, Z_FINISH);
        if (njs_slow_path(rc < 0)) {
            njs_vm_internal_error(vm, "failed to deflate the data: %s",
                                  stream.msg);
            goto fail;
        }

        njs_chb_written(&chain, chunk_size - stream.avail_out);

    } while (stream.avail_out == 0);

    deflateEnd(&stream);

    size = njs_chb_size(&chain);
    if (njs_slow_path(size < 0)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    buffer = njs_mp_alloc(njs_vm_memory_pool(vm), size);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    njs_chb_join_to(&chain, buffer);

    njs_chb_destroy(&chain);

    return njs_vm_value_buffer_set(vm, retval, buffer, size);

fail:

    deflateEnd(&stream);
    njs_chb_destroy(&chain);

    return NJS_ERROR;
}


static njs_int_t
njs_zlib_ext_inflate(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t raw, njs_value_t *retval)
{
    int                 rc, window_bits;
    u_char              *buffer;
    size_t              chunk_size;
    ssize_t             size;
    njs_chb_t           chain;
    z_stream            stream;
    njs_int_t           ret;
    njs_str_t           data, dictionary;
    njs_value_t         *options, *value;
    njs_opaque_value_t  lvalue;

    static const njs_str_t chunk_size_key = njs_str("chunkSize");
    static const njs_str_t dict_key = njs_str("dictionary");
    static const njs_str_t window_bits_key = njs_str("windowBits");

    ret = njs_vm_value_to_bytes(vm, &data, njs_arg(args, nargs, 1));
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    chunk_size = NJS_ZLIB_CHUNK_SIZE;
    dictionary.start = NULL;
    window_bits = raw ? -MAX_WBITS : MAX_WBITS;

    options = njs_arg(args, nargs, 2);

    if (njs_value_is_object(options)) {
        value = njs_vm_object_prop(vm, options, &chunk_size_key, &lvalue);
        if (value != NULL) {
            chunk_size = njs_value_number(value);

            if (njs_slow_path(chunk_size < 64)) {
                njs_vm_range_error(vm, "chunkSize must be >= 64");
                return NJS_ERROR;
            }
        }

        value = njs_vm_object_prop(vm, options, &window_bits_key, &lvalue);
        if (value != NULL) {
            window_bits = njs_value_number(value);

            if (raw) {
                if (njs_slow_path(window_bits < -15 || window_bits > -8)) {
                    njs_vm_range_error(vm, "windowBits must be in the range "
                                       "-15..-8");
                    return NJS_ERROR;
                }

            } else {
                if (njs_slow_path(window_bits < 8 || window_bits > 15)) {
                    njs_vm_range_error(vm, "windowBits must be in the range "
                                       "8..15");
                    return NJS_ERROR;
                }
            }
        }

        value = njs_vm_object_prop(vm, options, &dict_key, &lvalue);
        if (value != NULL) {
            ret = njs_vm_value_to_bytes(vm, &dictionary, value);
            if (njs_slow_path(ret != NJS_OK)) {
                return NJS_ERROR;
            }
        }
    }

    stream.next_in = data.start;
    stream.avail_in = data.length;

    stream.zalloc = njs_zlib_alloc;
    stream.zfree = njs_zlib_free;
    stream.opaque = njs_vm_memory_pool(vm);

    rc = inflateInit2(&stream, window_bits);
    if (njs_slow_path(rc != Z_OK)) {
        njs_vm_internal_error(vm, "inflateInit2() failed");
        return NJS_ERROR;
    }

    if (dictionary.start != NULL) {
        rc = inflateSetDictionary(&stream, dictionary.start, dictionary.length);
        if (njs_slow_path(rc != Z_OK)) {
            njs_vm_internal_error(vm, "deflateSetDictionary() failed");
            return NJS_ERROR;
        }
    }

    NJS_CHB_MP_INIT(&chain, njs_vm_memory_pool(vm));

    while (rc != Z_STREAM_END) {
        stream.next_out = njs_chb_reserve(&chain, chunk_size);
        if (njs_slow_path(stream.next_out == NULL)) {
            njs_vm_memory_error(vm);
            goto fail;
        }

        stream.avail_out = chunk_size;

        rc = inflate(&stream, Z_NO_FLUSH);
        if (njs_slow_path(rc < 0)) {
            njs_vm_internal_error(vm, "failed to inflate the compressed "
                                  "data: %s", stream.msg);
            goto fail;
        }

        if (rc == Z_NEED_DICT) {
            njs_vm_type_error(vm, "failed to inflate, dictionary is required");
            goto fail;
        }

        njs_chb_written(&chain, chunk_size - stream.avail_out);
    }

    rc = inflateEnd(&stream);
    if (njs_slow_path(rc != Z_OK)) {
        njs_vm_error(vm, "failed to end the inflate stream");
        return NJS_ERROR;
    }

    size = njs_chb_size(&chain);
    if (njs_slow_path(size < 0)) {
        njs_vm_memory_error(vm);
        return NJS_ERROR;
    }

    buffer = njs_mp_alloc(njs_vm_memory_pool(vm), size);
    if (njs_slow_path(buffer == NULL)) {
        return NJS_ERROR;
    }

    njs_chb_join_to(&chain, buffer);

    njs_chb_destroy(&chain);

    return njs_vm_value_buffer_set(vm, retval, buffer, size);

fail:

    inflateEnd(&stream);
    njs_chb_destroy(&chain);

    return NJS_ERROR;
}


njs_int_t
njs_zlib_constant(njs_vm_t *vm, njs_object_prop_t *prop, uint32_t unused,
    njs_value_t *value, njs_value_t *setval, njs_value_t *retval)
{
    njs_value_number_set(retval,  njs_vm_prop_magic32(prop));

    return NJS_OK;
}


static njs_int_t
njs_zlib_init(njs_vm_t *vm)
{
    njs_int_t           ret, proto_id;
    njs_mod_t           *module;
    njs_opaque_value_t  value;

    proto_id = njs_vm_external_prototype(vm, njs_ext_zlib,
                                         njs_nitems(njs_ext_zlib));
    if (njs_slow_path(proto_id < 0)) {
        return NJS_ERROR;
    }

    ret = njs_vm_external_create(vm, njs_value_arg(&value), proto_id, NULL, 1);
    if (njs_slow_path(ret != NJS_OK)) {
        return NJS_ERROR;
    }

    module = njs_vm_add_module(vm, &njs_str_value("zlib"),
                               njs_value_arg(&value));
    if (njs_slow_path(module == NULL)) {
        return NJS_ERROR;
    }

    return NJS_OK;
}


static void *
njs_zlib_alloc(void *opaque, u_int items, u_int size)
{
    return njs_mp_alloc(opaque, items * size);
}


static void
njs_zlib_free(void *opaque, void *address)
{
    /* Do nothing. */
}

