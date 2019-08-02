
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


uint32_t
njs_djb_hash(const void *data, size_t len)
{
    uint32_t      hash;
    const u_char  *p;

    p = data;
    hash = NJS_DJB_HASH_INIT;

    while (len != 0) {
        hash = njs_djb_hash_add(hash, *p++);
        len--;
    }

    return hash;
}


uint32_t
njs_djb_hash_lowcase(const void *data, size_t len)
{
    uint32_t      hash;
    const u_char  *p;

    p = data;
    hash = NJS_DJB_HASH_INIT;

    while (len != 0) {
        hash = njs_djb_hash_add(hash, njs_lower_case(*p++));
        len--;
    }

    return hash;
}
