
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NXT_MD5_H_INCLUDED_
#define _NXT_MD5_H_INCLUDED_


typedef struct {
    uint64_t  bytes;
    uint32_t  a, b, c, d;
    u_char    buffer[64];
} nxt_md5_t;


NXT_EXPORT void nxt_md5_init(nxt_md5_t *ctx);
NXT_EXPORT void nxt_md5_update(nxt_md5_t *ctx, const void *data, size_t size);
NXT_EXPORT void nxt_md5_final(u_char result[16], nxt_md5_t *ctx);

#endif /* _NXT_MD5_H_INCLUDED_ */
