
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NJS_MD5_H_INCLUDED_
#define _NJS_MD5_H_INCLUDED_


typedef struct {
    uint64_t  bytes;
    uint32_t  a, b, c, d;
    u_char    buffer[64];
} njs_md5_t;


NJS_EXPORT void njs_md5_init(njs_md5_t *ctx);
NJS_EXPORT void njs_md5_update(njs_md5_t *ctx, const void *data, size_t size);
NJS_EXPORT void njs_md5_final(u_char result[16], njs_md5_t *ctx);

#endif /* _NJS_MD5_H_INCLUDED_ */
