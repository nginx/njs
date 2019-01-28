
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_MP_H_INCLUDED_
#define _NXT_MP_H_INCLUDED_


typedef struct nxt_mp_s  nxt_mp_t;


NXT_EXPORT nxt_mp_t *nxt_mp_create(const nxt_mem_proto_t *proto, void *mem,
    void *trace, size_t cluster_size, size_t page_alignment, size_t page_size,
    size_t min_chunk_size)
    NXT_MALLOC_LIKE;
NXT_EXPORT nxt_mp_t * nxt_mp_fast_create(const nxt_mem_proto_t *proto,
    void *mem, void *trace, size_t cluster_size, size_t page_alignment,
    size_t page_size, size_t min_chunk_size)
    NXT_MALLOC_LIKE;
NXT_EXPORT nxt_bool_t nxt_mp_is_empty(nxt_mp_t *mp);
NXT_EXPORT void nxt_mp_destroy(nxt_mp_t *mp);

NXT_EXPORT void *nxt_mp_alloc(nxt_mp_t *mp, size_t size)
    NXT_MALLOC_LIKE;
NXT_EXPORT void *nxt_mp_zalloc(nxt_mp_t *mp, size_t size)
    NXT_MALLOC_LIKE;
NXT_EXPORT void *nxt_mp_align(nxt_mp_t *mp, size_t alignment, size_t size)
    NXT_MALLOC_LIKE;
NXT_EXPORT void *nxt_mp_zalign(nxt_mp_t *mp,
    size_t alignment, size_t size)
    NXT_MALLOC_LIKE;
NXT_EXPORT void nxt_mp_free(nxt_mp_t *mp, void *p);


#endif /* _NXT_MP_H_INCLUDED_ */
