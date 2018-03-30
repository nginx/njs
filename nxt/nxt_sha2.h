
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#ifndef _NXT_SHA2_H_INCLUDED_
#define _NXT_SHA2_H_INCLUDED_


typedef struct {
    uint64_t  bytes;
    uint32_t  a, b, c, d, e, f, g, h;
    u_char    buffer[64];
} nxt_sha2_t;


NXT_EXPORT void nxt_sha2_init(nxt_sha2_t *ctx);
NXT_EXPORT void nxt_sha2_update(nxt_sha2_t *ctx, const void *data, size_t size);
NXT_EXPORT void nxt_sha2_final(u_char result[32], nxt_sha2_t *ctx);


#endif /* _NXT_SHA2_H_INCLUDED_ */
