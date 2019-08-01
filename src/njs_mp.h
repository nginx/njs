
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_MP_H_INCLUDED_
#define _NJS_MP_H_INCLUDED_


typedef struct njs_mp_s  njs_mp_t;


NJS_EXPORT njs_mp_t *njs_mp_create(size_t cluster_size, size_t page_alignment,
    size_t page_size, size_t min_chunk_size) NJS_MALLOC_LIKE;
NJS_EXPORT njs_mp_t * njs_mp_fast_create(size_t cluster_size,
    size_t page_alignment, size_t page_size, size_t min_chunk_size)
    NJS_MALLOC_LIKE;
NJS_EXPORT njs_bool_t njs_mp_is_empty(njs_mp_t *mp);
NJS_EXPORT void njs_mp_destroy(njs_mp_t *mp);

NJS_EXPORT void *njs_mp_alloc(njs_mp_t *mp, size_t size)
    NJS_MALLOC_LIKE;
NJS_EXPORT void *njs_mp_zalloc(njs_mp_t *mp, size_t size)
    NJS_MALLOC_LIKE;
NJS_EXPORT void *njs_mp_align(njs_mp_t *mp, size_t alignment, size_t size)
    NJS_MALLOC_LIKE;
NJS_EXPORT void *njs_mp_zalign(njs_mp_t *mp,
    size_t alignment, size_t size)
    NJS_MALLOC_LIKE;
NJS_EXPORT void njs_mp_free(njs_mp_t *mp, void *p);


#if (NJS_ALLOC_DEBUG)
#define njs_debug_alloc(...)                                                  \
    njs_stderror(__VA_ARGS__)

#else

#define njs_debug_alloc(...)

#endif


#endif /* _NJS_MP_H_INCLUDED_ */
