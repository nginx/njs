
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NJS_HASH_H_INCLUDED_
#define _NJS_HASH_H_INCLUDED_


typedef struct {
    uint64_t  bytes;
    uint32_t  a, b, c, d, e, f, g, h;
    u_char    buffer[64];
} njs_hash_t;


NJS_EXPORT void njs_md5_init(njs_hash_t *ctx);
NJS_EXPORT void njs_md5_update(njs_hash_t *ctx, const void *data, size_t size);
NJS_EXPORT void njs_md5_final(u_char result[32], njs_hash_t *ctx);

NJS_EXPORT void njs_sha1_init(njs_hash_t *ctx);
NJS_EXPORT void njs_sha1_update(njs_hash_t *ctx, const void *data, size_t size);
NJS_EXPORT void njs_sha1_final(u_char result[32], njs_hash_t *ctx);

NJS_EXPORT void njs_sha2_init(njs_hash_t *ctx);
NJS_EXPORT void njs_sha2_update(njs_hash_t *ctx, const void *data, size_t size);
NJS_EXPORT void njs_sha2_final(u_char result[32], njs_hash_t *ctx);


#endif /* _NJS_HASH_H_INCLUDED_ */
