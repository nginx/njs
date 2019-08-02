
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t
lvlhsh_unit_test_key_test(njs_lvlhsh_query_t *lhq, void *data)
{
    if (*(uintptr_t *) lhq->key.start == (uintptr_t) data) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


static void *
lvlhsh_unit_test_pool_alloc(void *pool, size_t size)
{
    return njs_mp_align(pool, size, size);
}


static void
lvlhsh_unit_test_pool_free(void *pool, void *p, size_t size)
{
    njs_mp_free(pool, p);
}


static const njs_lvlhsh_proto_t  lvlhsh_proto  njs_aligned(64) = {
    NJS_LVLHSH_LARGE_SLAB,
    lvlhsh_unit_test_key_test,
    lvlhsh_unit_test_pool_alloc,
    lvlhsh_unit_test_pool_free,
};


static njs_int_t
lvlhsh_unit_test_add(njs_lvlhsh_t *lh, const njs_lvlhsh_proto_t *proto,
    void *pool, uintptr_t key)
{
    njs_lvlhsh_query_t  lhq;

    lhq.key_hash = key;
    lhq.replace = 0;
    lhq.key.length = sizeof(uintptr_t);
    lhq.key.start = (u_char *) &key;
    lhq.value = (void *) key;
    lhq.proto = proto;
    lhq.pool = pool;

    switch (njs_lvlhsh_insert(lh, &lhq)) {

    case NJS_OK:
        return NJS_OK;

    case NJS_DECLINED:
        njs_printf("lvlhsh unit test failed: key %08Xl is already in hash\n",
                   (long) key);
        /* Fall through. */

    default:
        return NJS_ERROR;
    }
}


static njs_int_t
lvlhsh_unit_test_get(njs_lvlhsh_t *lh, const njs_lvlhsh_proto_t *proto,
    uintptr_t key)
{
    njs_lvlhsh_query_t  lhq;

    lhq.key_hash = key;
    lhq.key.length = sizeof(uintptr_t);
    lhq.key.start = (u_char *) &key;
    lhq.proto = proto;

    if (njs_lvlhsh_find(lh, &lhq) == NJS_OK) {

        if (key == (uintptr_t) lhq.value) {
            return NJS_OK;
        }
    }

    njs_printf("lvlhsh unit test failed: key %08Xl not found in hash\n",
               (long) key);

    return NJS_ERROR;
}


static njs_int_t
lvlhsh_unit_test_delete(njs_lvlhsh_t *lh, const njs_lvlhsh_proto_t *proto,
    void *pool, uintptr_t key)
{
    njs_int_t           ret;
    njs_lvlhsh_query_t  lhq;

    lhq.key_hash = key;
    lhq.key.length = sizeof(uintptr_t);
    lhq.key.start = (u_char *) &key;
    lhq.proto = proto;
    lhq.pool = pool;

    ret = njs_lvlhsh_delete(lh, &lhq);

    if (ret != NJS_OK) {
        njs_printf("lvlhsh unit test failed: key %08lX not found in hash\n",
               (long) key);
    }

    return ret;
}


static njs_int_t
lvlhsh_unit_test(njs_uint_t n)
{
    njs_mp_t           *pool;
    uint32_t           key;
    njs_uint_t         i;
    njs_lvlhsh_t       lh;
    njs_lvlhsh_each_t  lhe;

    const size_t       min_chunk_size = 32;
    const size_t       page_size = 1024;
    const size_t       page_alignment = 128;
    const size_t       cluster_size = 4096;

    pool = njs_mp_create(cluster_size, page_alignment, page_size,
                         min_chunk_size);
    if (pool == NULL) {
        return NJS_ERROR;
    }

    njs_printf("lvlhsh unit test started: %l items\n", (long) n);

    njs_memzero(&lh, sizeof(njs_lvlhsh_t));

    key = 0;
    for (i = 0; i < n; i++) {
        key = njs_murmur_hash2(&key, sizeof(uint32_t));

        if (lvlhsh_unit_test_add(&lh, &lvlhsh_proto, pool, key) != NJS_OK) {
            njs_printf("lvlhsh add unit test failed at %l\n", (long) i);
            return NJS_ERROR;
        }
    }

    key = 0;
    for (i = 0; i < n; i++) {
        key = njs_murmur_hash2(&key, sizeof(uint32_t));

        if (lvlhsh_unit_test_get(&lh, &lvlhsh_proto, key) != NJS_OK) {
            return NJS_ERROR;
        }
    }

    njs_lvlhsh_each_init(&lhe, &lvlhsh_proto);

    for (i = 0; i < n + 1; i++) {
        if (njs_lvlhsh_each(&lh, &lhe) == NULL) {
            break;
        }
    }

    if (i != n) {
        njs_printf("lvlhsh each unit test failed at %l of %l\n",
                   (long) i, (long) n);
        return NJS_ERROR;
    }

    key = 0;
    for (i = 0; i < n; i++) {
        key = njs_murmur_hash2(&key, sizeof(uint32_t));

        if (lvlhsh_unit_test_delete(&lh, &lvlhsh_proto, pool, key) != NJS_OK) {
            return NJS_ERROR;
        }
    }

    if (!njs_mp_is_empty(pool)) {
        njs_printf("mem cache pool is not empty\n");
        return NJS_ERROR;
    }

    njs_mp_destroy(pool);

    njs_printf("lvlhsh unit test passed\n");

    return NJS_OK;
}


int
main(void)
{
     return lvlhsh_unit_test(1000 * 1000);
}
