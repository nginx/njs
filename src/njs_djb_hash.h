
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_DJB_HASH_H_INCLUDED_
#define _NJS_DJB_HASH_H_INCLUDED_


/* A fast and simple hash function by Daniel J. Bernstein. */


NJS_EXPORT uint32_t njs_djb_hash(const void *data, size_t len);
NJS_EXPORT uint32_t njs_djb_hash_lowcase(const void *data, size_t len);


#define NJS_DJB_HASH_INIT  5381


#define njs_djb_hash_add(hash, val)                                           \
    ((uint32_t) ((((hash) << 5) + (hash)) ^ (uint32_t) (val)))


#endif /* _NJS_DJB_HASH_H_INCLUDED_ */
