
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_sprintf.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_malloc.h>
#include <nxt_lvlhsh.h>
#include <nxt_murmur_hash.h>
#include <nxt_mp.h>
#include <string.h>


static nxt_int_t
lvlhsh_unit_test_key_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    if (*(uintptr_t *) lhq->key.start == (uintptr_t) data) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


static void *
lvlhsh_unit_test_pool_alloc(void *pool, size_t size, nxt_uint_t nalloc)
{
    return nxt_mp_align(pool, size, size);
}


static void
lvlhsh_unit_test_pool_free(void *pool, void *p, size_t size)
{
    nxt_mp_free(pool, p);
}


static const nxt_lvlhsh_proto_t  lvlhsh_proto  nxt_aligned(64) = {
    NXT_LVLHSH_LARGE_SLAB,
    0,
    lvlhsh_unit_test_key_test,
    lvlhsh_unit_test_pool_alloc,
    lvlhsh_unit_test_pool_free,
};


static nxt_int_t
lvlhsh_unit_test_add(nxt_lvlhsh_t *lh, const nxt_lvlhsh_proto_t *proto,
    void *pool, uintptr_t key)
{
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = key;
    lhq.replace = 0;
    lhq.key.length = sizeof(uintptr_t);
    lhq.key.start = (u_char *) &key;
    lhq.value = (void *) key;
    lhq.proto = proto;
    lhq.pool = pool;

    switch (nxt_lvlhsh_insert(lh, &lhq)) {

    case NXT_OK:
        return NXT_OK;

    case NXT_DECLINED:
        nxt_printf("lvlhsh unit test failed: key %08Xl is already in hash\n",
                   (long) key);
        /* Fall through. */

    default:
        return NXT_ERROR;
    }
}


static nxt_int_t
lvlhsh_unit_test_get(nxt_lvlhsh_t *lh, const nxt_lvlhsh_proto_t *proto,
    uintptr_t key)
{
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = key;
    lhq.key.length = sizeof(uintptr_t);
    lhq.key.start = (u_char *) &key;
    lhq.proto = proto;

    if (nxt_lvlhsh_find(lh, &lhq) == NXT_OK) {

        if (key == (uintptr_t) lhq.value) {
            return NXT_OK;
        }
    }

    nxt_printf("lvlhsh unit test failed: key %08Xl not found in hash\n",
               (long) key);

    return NXT_ERROR;
}


static nxt_int_t
lvlhsh_unit_test_delete(nxt_lvlhsh_t *lh, const nxt_lvlhsh_proto_t *proto,
    void *pool, uintptr_t key)
{
    nxt_int_t           ret;
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = key;
    lhq.key.length = sizeof(uintptr_t);
    lhq.key.start = (u_char *) &key;
    lhq.proto = proto;
    lhq.pool = pool;

    ret = nxt_lvlhsh_delete(lh, &lhq);

    if (ret != NXT_OK) {
        nxt_printf("lvlhsh unit test failed: key %08lX not found in hash\n",
               (long) key);
    }

    return ret;
}


static void *
lvlhsh_malloc(void *mem, size_t size)
{
    return nxt_malloc(size);
}


static void *
lvlhsh_zalloc(void *mem, size_t size)
{
    void  *p;

    p = nxt_malloc(size);

    if (p != NULL) {
        nxt_memzero(p, size);
    }

    return p;
}


static void *
lvlhsh_align(void *mem, size_t alignment, size_t size)
{
    return nxt_memalign(alignment, size);
}


static void
lvlhsh_free(void *mem, void *p)
{
    nxt_free(p);
}


static void
lvlhsh_alert(void *mem, const char *fmt, ...)
{
    u_char   buf[1024], *p;
    va_list  args;

    va_start(args, fmt);
    p = nxt_sprintf(buf, buf + sizeof(buf), fmt, args);
    va_end(args);

    (void) nxt_error("alert: \"%*s\"\n", p - buf, buf);
}


static const nxt_mem_proto_t  lvl_mp_proto = {
    lvlhsh_malloc,
    lvlhsh_zalloc,
    lvlhsh_align,
    NULL,
    lvlhsh_free,
    lvlhsh_alert,
    NULL,
};


static nxt_int_t
lvlhsh_unit_test(nxt_uint_t n)
{
    nxt_mp_t           *pool;
    uint32_t           key;
    nxt_uint_t         i;
    nxt_lvlhsh_t       lh;
    nxt_lvlhsh_each_t  lhe;

    const size_t       min_chunk_size = 32;
    const size_t       page_size = 1024;
    const size_t       page_alignment = 128;
    const size_t       cluster_size = 4096;

    pool = nxt_mp_create(&lvl_mp_proto, NULL, NULL, cluster_size,
                         page_alignment, page_size, min_chunk_size);
    if (pool == NULL) {
        return NXT_ERROR;
    }

    nxt_printf("lvlhsh unit test started: %l items\n", (long) n);

    nxt_memzero(&lh, sizeof(nxt_lvlhsh_t));

    key = 0;
    for (i = 0; i < n; i++) {
        key = nxt_murmur_hash2(&key, sizeof(uint32_t));

        if (lvlhsh_unit_test_add(&lh, &lvlhsh_proto, pool, key) != NXT_OK) {
            nxt_printf("lvlhsh add unit test failed at %l\n", (long) i);
            return NXT_ERROR;
        }
    }

    key = 0;
    for (i = 0; i < n; i++) {
        key = nxt_murmur_hash2(&key, sizeof(uint32_t));

        if (lvlhsh_unit_test_get(&lh, &lvlhsh_proto, key) != NXT_OK) {
            return NXT_ERROR;
        }
    }

    nxt_lvlhsh_each_init(&lhe, &lvlhsh_proto);

    for (i = 0; i < n + 1; i++) {
        if (nxt_lvlhsh_each(&lh, &lhe) == NULL) {
            break;
        }
    }

    if (i != n) {
        nxt_printf("lvlhsh each unit test failed at %l of %l\n",
                   (long) i, (long) n);
        return NXT_ERROR;
    }

    key = 0;
    for (i = 0; i < n; i++) {
        key = nxt_murmur_hash2(&key, sizeof(uint32_t));

        if (lvlhsh_unit_test_delete(&lh, &lvlhsh_proto, pool, key) != NXT_OK) {
            return NXT_ERROR;
        }
    }

    if (!nxt_mp_is_empty(pool)) {
        nxt_printf("mem cache pool is not empty\n");
        return NXT_ERROR;
    }

    nxt_mp_destroy(pool);

    nxt_printf("lvlhsh unit test passed\n");

    return NXT_OK;
}


int
main(void)
{
     return lvlhsh_unit_test(1000 * 1000);
}
