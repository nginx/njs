
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_djb_hash.h>


uint32_t
nxt_djb_hash(const void *data, size_t len)
{
    uint32_t      hash;
    const u_char  *p;

    p = data;
    hash = NXT_DJB_HASH_INIT;

    while (len != 0) {
        hash = nxt_djb_hash_add(hash, *p++);
        len--;
    }

    return hash;
}


uint32_t
nxt_djb_hash_lowcase(const void *data, size_t len)
{
    uint32_t      hash;
    const u_char  *p;

    p = data;
    hash = NXT_DJB_HASH_INIT;

    while (len != 0) {
        hash = nxt_djb_hash_add(hash, nxt_lower_case(*p++));
        len--;
    }

    return hash;
}
