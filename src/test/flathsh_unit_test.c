
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static njs_int_t
flathsh_unit_test_key_test(njs_flathsh_query_t *fhq, void *data)
{
    if (*(uintptr_t *) fhq->key.start == *(uintptr_t *) data) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


static void *
flathsh_unit_test_pool_alloc(void *pool, size_t size)
{
    return njs_mp_align(pool, NJS_MAX_ALIGNMENT, size);
}


static void
flathsh_unit_test_pool_free(void *pool, void *p, size_t size)
{
    njs_mp_free(pool, p);
}


static const njs_flathsh_proto_t  flathsh_proto  njs_aligned(64) = {
    flathsh_unit_test_key_test,
    flathsh_unit_test_pool_alloc,
    flathsh_unit_test_pool_free,
};


static njs_int_t
flathsh_unit_test_add(njs_flathsh_t *lh, const njs_flathsh_proto_t *proto,
    void *pool, uintptr_t key)
{
    njs_flathsh_query_t  fhq;

    fhq.key_hash = key;
    fhq.replace = 0;
    fhq.key.length = sizeof(uintptr_t);
    fhq.key.start = (u_char *) &key;
    fhq.proto = proto;
    fhq.pool = pool;

    switch (njs_flathsh_insert(lh, &fhq)) {

    case NJS_OK:
        ((njs_flathsh_elt_t *) fhq.value)->value[0] = (void *) key;
        return NJS_OK;

    case NJS_DECLINED:
        njs_printf("flathsh unit test failed: key %08Xl is already in hash\n",
                   (long) key);
        /* Fall through. */

    default:
        return NJS_ERROR;
    }
}


static njs_int_t
flathsh_unit_test_get(njs_flathsh_t *lh, const njs_flathsh_proto_t *proto,
    uintptr_t key)
{
    njs_flathsh_query_t  fhq;

    fhq.key_hash = key;
    fhq.key.length = sizeof(uintptr_t);
    fhq.key.start = (u_char *) &key;
    fhq.proto = proto;

    if (njs_flathsh_find(lh, &fhq) == NJS_OK) {

        if (key == (uintptr_t) ((njs_flathsh_elt_t *) fhq.value)->value[0]) {
            return NJS_OK;
        }
    }

    njs_printf("flathsh unit test failed: key %08Xl not found in hash\n",
               (long) key);

    return NJS_ERROR;
}


static njs_int_t
flathsh_unit_test_delete(njs_flathsh_t *lh, const njs_flathsh_proto_t *proto,
    void *pool, uintptr_t key)
{
    njs_int_t            ret;
    njs_flathsh_query_t  fhq;

    fhq.key_hash = key;
    fhq.key.length = sizeof(uintptr_t);
    fhq.key.start = (u_char *) &key;
    fhq.proto = proto;
    fhq.pool = pool;

    ret = njs_flathsh_delete(lh, &fhq);

    if (ret != NJS_OK) {
        njs_printf("flathsh unit test failed: key %08lX not found in hash\n",
                   (long) key);
    }

    return ret;
}


static njs_int_t
flathsh_unit_test(njs_uint_t n)
{
    njs_mp_t            *pool;
    uint32_t            key;
    njs_uint_t          i;
    njs_flathsh_t       lh;
    njs_flathsh_each_t  lhe;

    const size_t       min_chunk_size = 32;
    const size_t       page_size = 1024;
    const size_t       page_alignment = 128;
    const size_t       cluster_size = 4096;

    pool = njs_mp_create(cluster_size, page_alignment, page_size,
                         min_chunk_size);
    if (pool == NULL) {
        return NJS_ERROR;
    }

    njs_printf("flathsh unit test started: %l items\n", (long) n);

    njs_memzero(&lh, sizeof(njs_flathsh_t));

    key = 0;
    for (i = 0; i < n; i++) {
        key = njs_murmur_hash2(&key, sizeof(uint32_t));

        if (flathsh_unit_test_add(&lh, &flathsh_proto, pool, key) != NJS_OK) {
            njs_printf("flathsh add unit test failed at %l\n", (long) i);
            return NJS_ERROR;
        }
    }

    key = 0;
    for (i = 0; i < n; i++) {
        key = njs_murmur_hash2(&key, sizeof(uint32_t));

        if (flathsh_unit_test_get(&lh, &flathsh_proto, key) != NJS_OK) {
            return NJS_ERROR;
        }
    }

    njs_flathsh_each_init(&lhe, &flathsh_proto);

    for (i = 0; i < n + 1; i++) {
        if (njs_flathsh_each(&lh, &lhe) == NULL) {
            break;
        }
    }

    if (i != n) {
        njs_printf("flathsh each unit test failed at %l of %l\n",
                   (long) i, (long) n);
        return NJS_ERROR;
    }

    key = 0;
    for (i = 0; i < n; i++) {
        key = njs_murmur_hash2(&key, sizeof(uint32_t));

        if (flathsh_unit_test_delete(&lh, &flathsh_proto, pool, key) != NJS_OK) {
            return NJS_ERROR;
        }
    }

    if (!njs_mp_is_empty(pool)) {
        njs_printf("mem cache pool is not empty\n");
        return NJS_ERROR;
    }

    njs_mp_destroy(pool);

    njs_printf("flathsh unit test passed\n");

    return NJS_OK;
}


int
main(void)
{
     return flathsh_unit_test(1000 * 1000);
}
