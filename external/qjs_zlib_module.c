
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) F5, Inc.
 */

#include <qjs.h>
#include <zlib.h>

#define NJS_ZLIB_CHUNK_SIZE  1024

static JSValue qjs_zlib_ext_deflate(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int raw);
static JSValue qjs_zlib_ext_inflate(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int raw);

static JSModuleDef *qjs_zlib_init(JSContext *ctx, const char *name);
static void *qjs_zlib_alloc(void *opaque, u_int items, u_int size);
static void qjs_zlib_free(void *opaque, void *address);


static const JSCFunctionListEntry qjs_zlib_constants[] = {
    JS_PROP_INT32_DEF("Z_NO_COMPRESSION",
                      Z_NO_COMPRESSION,
                      JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Z_BEST_SPEED",
                      Z_BEST_SPEED,
                      JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Z_BEST_COMPRESSION",
                      Z_BEST_COMPRESSION,
                      JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Z_FILTERED",
                      Z_FILTERED,
                      JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Z_HUFFMAN_ONLY",
                      Z_HUFFMAN_ONLY,
                      JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Z_RLE",
                      Z_RLE,
                      JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Z_FIXED",
                      Z_FIXED,
                      JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("Z_DEFAULT_STRATEGY",
                      Z_DEFAULT_STRATEGY,
                      JS_PROP_ENUMERABLE),
};

static const JSCFunctionListEntry qjs_zlib_export[] = {
    JS_CFUNC_MAGIC_DEF("deflateRawSync", 2, qjs_zlib_ext_deflate, 1),
    JS_CFUNC_MAGIC_DEF("deflateSync", 2, qjs_zlib_ext_deflate, 0),
    JS_CFUNC_MAGIC_DEF("inflateRawSync", 2, qjs_zlib_ext_inflate, 1),
    JS_CFUNC_MAGIC_DEF("inflateSync", 2, qjs_zlib_ext_inflate, 0),
    JS_OBJECT_DEF("constants",
                  qjs_zlib_constants,
                  njs_nitems(qjs_zlib_constants),
                  JS_PROP_CONFIGURABLE),
};


qjs_module_t  qjs_zlib_module = {
    .name = "zlib",
    .init = qjs_zlib_init,
};


static JSValue
qjs_zlib_ext_deflate(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int raw)
{
    int          rc, chunk_size, level, mem_level, strategy, window_bits;
    JSValue      ret, options;
    z_stream     stream;
    njs_chb_t    chain;
    qjs_bytes_t  bytes, dictionary;

    chunk_size = NJS_ZLIB_CHUNK_SIZE;
    mem_level = 8;
    level = Z_DEFAULT_COMPRESSION;
    strategy = Z_DEFAULT_STRATEGY;
    window_bits = raw ? -MAX_WBITS : MAX_WBITS;

    NJS_CHB_CTX_INIT(&chain, ctx);
    dictionary.start = NULL;
    dictionary.length = 0;
    stream.opaque = NULL;

    options = argv[1];

    if (JS_IsObject(options)) {
        ret = JS_GetPropertyStr(ctx, options, "chunkSize");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(ret)) {
            rc = JS_ToInt32(ctx, &chunk_size, ret);
            JS_FreeValue(ctx, ret);
            if (rc != 0) {
                return JS_EXCEPTION;
            }

            if (chunk_size < 64) {
                JS_ThrowRangeError(ctx, "chunkSize must be >= 64");
                return JS_EXCEPTION;
            }
        }

        ret = JS_GetPropertyStr(ctx, options, "level");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(ret)) {
            rc = JS_ToInt32(ctx, &level, ret);
            JS_FreeValue(ctx, ret);
            if (rc != 0) {
                return JS_EXCEPTION;
            }

            if (level < Z_DEFAULT_COMPRESSION || level > Z_BEST_COMPRESSION) {
                JS_ThrowRangeError(ctx, "level must be in the range %d..%d",
                                   Z_DEFAULT_COMPRESSION, Z_BEST_COMPRESSION);
                return JS_EXCEPTION;
            }
        }

        ret = JS_GetPropertyStr(ctx, options, "windowBits");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(ret)) {
            rc = JS_ToInt32(ctx, &window_bits, ret);
            JS_FreeValue(ctx, ret);
            if (rc != 0) {
                return JS_EXCEPTION;
            }

            if (raw) {
                if (window_bits < -15 || window_bits > -9) {
                    JS_ThrowRangeError(ctx, "windowBits must be in the range "
                                       "-15..-9");
                    return JS_EXCEPTION;
                }

            } else {
                if (window_bits < 9 || window_bits > 15) {
                    JS_ThrowRangeError(ctx, "windowBits must be in the range "
                                       "9..15");
                    return JS_EXCEPTION;
                }
            }
        }

        ret = JS_GetPropertyStr(ctx, options, "memLevel");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(ret)) {
            rc = JS_ToInt32(ctx, &mem_level, ret);
            JS_FreeValue(ctx, ret);
            if (rc != 0) {
                return JS_EXCEPTION;
            }

            if (mem_level < 1 || mem_level > 9) {
                JS_ThrowRangeError(ctx, "memLevel must be in the range 1..9");
                return JS_EXCEPTION;
            }
        }

        ret = JS_GetPropertyStr(ctx, options, "strategy");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(ret)) {
            rc = JS_ToInt32(ctx, &strategy, ret);
            JS_FreeValue(ctx, ret);
            if (rc != 0) {
                return JS_EXCEPTION;
            }

            switch (strategy) {
            case Z_FILTERED:
            case Z_HUFFMAN_ONLY:
            case Z_RLE:
            case Z_FIXED:
            case Z_DEFAULT_STRATEGY:
                break;

            default:
                JS_ThrowRangeError(ctx, "unknown strategy: %d", strategy);
                return JS_EXCEPTION;
            }
        }

        ret = JS_GetPropertyStr(ctx, options, "dictionary");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(ret)) {
            rc = qjs_to_bytes(ctx, &dictionary, ret);
            JS_FreeValue(ctx, ret);
            if (rc != 0) {
                return JS_EXCEPTION;
            }
        }
    }

    rc = qjs_to_bytes(ctx, &bytes, argv[0]);
    if (rc != 0) {
        return JS_EXCEPTION;
    }

    stream.next_in = bytes.start;
    stream.avail_in = bytes.length;

    stream.zalloc = qjs_zlib_alloc;
    stream.zfree = qjs_zlib_free;
    stream.opaque = ctx;

    rc = deflateInit2(&stream, level, Z_DEFLATED, window_bits, mem_level,
                      strategy);
    if (njs_slow_path(rc != Z_OK)) {
        JS_ThrowInternalError(ctx, "deflateInit2() failed");
        goto fail;
    }

    if (dictionary.start != NULL) {
        rc = deflateSetDictionary(&stream, dictionary.start, dictionary.length);
        if (rc != Z_OK) {
            JS_ThrowInternalError(ctx, "deflateSetDictionary() failed");
            goto fail;
        }
    }

    do {
        stream.next_out = njs_chb_reserve(&chain, chunk_size);
        if (njs_slow_path(stream.next_out == NULL)) {
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }

        stream.avail_out = chunk_size;

        rc = deflate(&stream, Z_FINISH);
        if (njs_slow_path(rc < 0)) {
            JS_ThrowInternalError(ctx, "failed to deflate the data: %s",
                                  stream.msg);
            goto fail;
        }

        njs_chb_written(&chain, chunk_size - stream.avail_out);

    } while (stream.avail_out == 0);

    deflateEnd(&stream);

    qjs_bytes_free(ctx, &bytes);

    if (dictionary.start != NULL) {
        qjs_bytes_free(ctx, &dictionary);
    }

    ret = qjs_buffer_chb_alloc(ctx, &chain);

    njs_chb_destroy(&chain);

    return ret;

fail:

    qjs_bytes_free(ctx, &bytes);

    if (dictionary.start != NULL) {
        qjs_bytes_free(ctx, &dictionary);
    }

    if (stream.opaque != NULL) {
        deflateEnd(&stream);
    }

    if (chain.pool != NULL) {
        njs_chb_destroy(&chain);
    }

    return JS_EXCEPTION;
}


static JSValue
qjs_zlib_ext_inflate(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int raw)
{
    int          rc, chunk_size, window_bits;
    JSValue      ret, options;
    z_stream     stream;
    njs_chb_t    chain;
    qjs_bytes_t  bytes, dictionary;

    chunk_size = NJS_ZLIB_CHUNK_SIZE;
    window_bits = raw ? -MAX_WBITS : MAX_WBITS;

    NJS_CHB_CTX_INIT(&chain, ctx);
    dictionary.start = NULL;
    dictionary.length = 0;
    stream.opaque = NULL;

    options = argv[1];

    if (JS_IsObject(options)) {
        ret = JS_GetPropertyStr(ctx, options, "chunkSize");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(ret)) {
            rc = JS_ToInt32(ctx, &chunk_size, ret);
            JS_FreeValue(ctx, ret);
            if (rc != 0) {
                return JS_EXCEPTION;
            }

            if (chunk_size < 64) {
                JS_ThrowRangeError(ctx, "chunkSize must be >= 64");
                return JS_EXCEPTION;
            }
        }

        ret = JS_GetPropertyStr(ctx, options, "windowBits");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(ret)) {
            rc = JS_ToInt32(ctx, &window_bits, ret);
            JS_FreeValue(ctx, ret);
            if (rc != 0) {
                return JS_EXCEPTION;
            }

            if (raw) {
                if (window_bits < -15 || window_bits > -8) {
                    JS_ThrowRangeError(ctx, "windowBits must be in the range "
                                       "-15..-8");
                    return JS_EXCEPTION;
                }

            } else {
                if (window_bits < 8 || window_bits > 15) {
                    JS_ThrowRangeError(ctx, "windowBits must be in the range "
                                       "8..15");
                    return JS_EXCEPTION;
                }
            }
        }

        ret = JS_GetPropertyStr(ctx, options, "dictionary");
        if (JS_IsException(ret)) {
            return JS_EXCEPTION;
        }

        if (!JS_IsUndefined(ret)) {
            rc = qjs_to_bytes(ctx, &dictionary, ret);
            JS_FreeValue(ctx, ret);
            if (rc != 0) {
                return JS_EXCEPTION;
            }
        }
    }

    rc = qjs_to_bytes(ctx, &bytes, argv[0]);
    if (rc != 0) {
        return JS_EXCEPTION;
    }

    stream.next_in = bytes.start;
    stream.avail_in = bytes.length;

    stream.zalloc = qjs_zlib_alloc;
    stream.zfree = qjs_zlib_free;
    stream.opaque = ctx;

    rc = inflateInit2(&stream, window_bits);
    if (njs_slow_path(rc != Z_OK)) {
        JS_ThrowInternalError(ctx, "inflateInit2() failed");
        goto fail;
    }

    if (dictionary.start != NULL) {
        rc = inflateSetDictionary(&stream, dictionary.start, dictionary.length);
        if (rc != Z_OK) {
            JS_ThrowInternalError(ctx, "inflateSetDictionary() failed");
            goto fail;
        }
    }

    while (rc != Z_STREAM_END) {
        stream.next_out = njs_chb_reserve(&chain, chunk_size);
        if (njs_slow_path(stream.next_out == NULL)) {
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }

        stream.avail_out = chunk_size;

        rc = inflate(&stream, Z_NO_FLUSH);
        if (njs_slow_path(rc < 0)) {
            JS_ThrowInternalError(ctx, "failed to inflate the data: %s",
                                  stream.msg);
            goto fail;
        }

        njs_chb_written(&chain, chunk_size - stream.avail_out);
    }

    rc = inflateEnd(&stream);
    if (njs_slow_path(rc != Z_OK)) {
        JS_ThrowInternalError(ctx, "inflateEnd() failed");
        goto fail;
    }

    qjs_bytes_free(ctx, &bytes);

    if (dictionary.start != NULL) {
        qjs_bytes_free(ctx, &dictionary);
    }

    ret = qjs_buffer_chb_alloc(ctx, &chain);

    njs_chb_destroy(&chain);

    return ret;

fail:

    qjs_bytes_free(ctx, &bytes);

    if (dictionary.start != NULL) {
        qjs_bytes_free(ctx, &dictionary);
    }

    if (stream.opaque != NULL) {
        inflateEnd(&stream);
    }

    if (chain.pool != NULL) {
        njs_chb_destroy(&chain);
    }

    return JS_EXCEPTION;
}


static int
qjs_zlib_module_init(JSContext *ctx, JSModuleDef *m)
{
    int      rc;
    JSValue  proto;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, qjs_zlib_export,
                               njs_nitems(qjs_zlib_export));

    rc = JS_SetModuleExport(ctx, m, "default", proto);
    if (rc != 0) {
        return -1;
    }

    return JS_SetModuleExportList(ctx, m, qjs_zlib_export,
                                  njs_nitems(qjs_zlib_export));
}


static JSModuleDef *
qjs_zlib_init(JSContext *ctx, const char *name)
{
    int          rc;
    JSModuleDef  *m;

    m = JS_NewCModule(ctx, name, qjs_zlib_module_init);
    if (m == NULL) {
        return NULL;
    }

    JS_AddModuleExport(ctx, m, "default");
    rc = JS_AddModuleExportList(ctx, m, qjs_zlib_export,
                                njs_nitems(qjs_zlib_export));
    if (rc != 0) {
        return NULL;
    }

    return m;
}


static void *
qjs_zlib_alloc(void *opaque, u_int items, u_int size)
{
    return js_malloc(opaque, items * size);
}


static void
qjs_zlib_free(void *opaque, void *address)
{
    js_free(opaque, address);
}
