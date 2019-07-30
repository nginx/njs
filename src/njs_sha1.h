
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NJS_SHA1_H_INCLUDED_
#define _NJS_SHA1_H_INCLUDED_


typedef struct {
    uint64_t  bytes;
    uint32_t  a, b, c, d, e;
    u_char    buffer[64];
} njs_sha1_t;


NJS_EXPORT void njs_sha1_init(njs_sha1_t *ctx);
NJS_EXPORT void njs_sha1_update(njs_sha1_t *ctx, const void *data, size_t size);
NJS_EXPORT void njs_sha1_final(u_char result[20], njs_sha1_t *ctx);


#endif /* _NJS_SHA1_H_INCLUDED_ */
