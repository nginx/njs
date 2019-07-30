
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_MURMUR_HASH_H_INCLUDED_
#define _NJS_MURMUR_HASH_H_INCLUDED_


NJS_EXPORT uint32_t njs_murmur_hash2(const void *data, size_t len);
NJS_EXPORT uint32_t njs_murmur_hash2_uint32(const void *data);


#endif /* _NJS_MURMUR_HASH_H_INCLUDED_ */
