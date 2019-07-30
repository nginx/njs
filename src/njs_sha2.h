
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NJS_SHA2_H_INCLUDED_
#define _NJS_SHA2_H_INCLUDED_


typedef struct {
    uint64_t  bytes;
    uint32_t  a, b, c, d, e, f, g, h;
    u_char    buffer[64];
} njs_sha2_t;


NJS_EXPORT void njs_sha2_init(njs_sha2_t *ctx);
NJS_EXPORT void njs_sha2_update(njs_sha2_t *ctx, const void *data, size_t size);
NJS_EXPORT void njs_sha2_final(u_char result[32], njs_sha2_t *ctx);


#endif /* _NJS_SHA2_H_INCLUDED_ */
