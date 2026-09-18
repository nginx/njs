
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    size_t      cluster_size;
    size_t      page_alignment;
    size_t      page_size;
    size_t      min_chunk_size;
} njs_mp_test_conf_t;


typedef struct {
    uintptr_t   start;
    size_t      size;
    u_char      tag;
} njs_mp_test_item_t;


typedef enum {
    NJS_MP_TEST_FORWARD = 0,
    NJS_MP_TEST_REVERSE,
    NJS_MP_TEST_RANDOM,
} njs_mp_test_order_t;


static const njs_mp_test_conf_t  njs_mp_test_confs[] = {
    /* Typical configuration on a host with 4K pages. */
    { 8192, 128, 512, 16 },

    /* Cluster size is not a power of two. */
    { 512 * 3, 128, 512, 16 },
    { 512 * 24, 128, 512, 16 },

    /* The maximum of 256 pages in a cluster. */
    { 512 * 256, 128, 512, 16 },

    /* A single page in a cluster. */
    { 512, 128, 512, 16 },

    /* The maximum of 32 chunks in a page. */
    { 4096, 128, 1024, 32 },
    { 64 * 256, 64, 64, 16 },
};


static uint32_t
njs_mp_test_rand(uint32_t *state)
{
    uint32_t  n;

    n = *state;
    n ^= n << 13;
    n ^= n >> 17;
    n ^= n << 5;

    *state = n;

    return *state;
}


static int njs_cdecl
njs_mp_test_item_cmp(const void *one, const void *two, void *ctx)
{
    const njs_mp_test_item_t  *first, *second;

    first = one;
    second = two;

    if (first->start < second->start) {
        return -1;
    }

    if (first->start == second->start) {
        return 0;
    }

    return 1;
}


static njs_int_t
njs_mp_test_fill(njs_mp_test_item_t *items, njs_uint_t i, u_char *p,
    size_t size)
{
    if (p == NULL) {
        njs_printf("mp test: allocation of %uz failed\n", size);
        return NJS_ERROR;
    }

    items[i].start = (uintptr_t) p;
    items[i].size = size;
    items[i].tag = (u_char) (i + 1);

    njs_memset(p, items[i].tag, size);

    return NJS_OK;
}


static njs_int_t
njs_mp_test_verify(njs_mp_test_item_t *items, njs_uint_t i)
{
    size_t  n;
    u_char  *p;

    p = (u_char *) items[i].start;

    for (n = 0; n < items[i].size; n++) {
        if (p[n] != items[i].tag) {
            njs_printf("mp test: block %uz of %uz is corrupted at %uz\n",
                       (size_t) i, items[i].size, (size_t) n);
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


/* Verify that no two live allocations overlap. */

static njs_int_t
njs_mp_test_disjoint(njs_mp_test_item_t *items, njs_uint_t n)
{
    njs_uint_t          i;
    njs_mp_test_item_t  *sorted;

    sorted = malloc(n * sizeof(njs_mp_test_item_t));
    if (sorted == NULL) {
        return NJS_ERROR;
    }

    memcpy(sorted, items, n * sizeof(njs_mp_test_item_t));

    njs_qsort(sorted, n, sizeof(njs_mp_test_item_t), njs_mp_test_item_cmp,
              NULL);

    for (i = 1; i < n; i++) {
        if (sorted[i - 1].size
            > sorted[i].start - sorted[i - 1].start)
        {
            njs_printf("mp test: blocks %p:%uz and %p:%uz overlap\n",
                       (void *) sorted[i - 1].start, sorted[i - 1].size,
                       (void *) sorted[i].start, sorted[i].size);
            free(sorted);
            return NJS_ERROR;
        }
    }

    free(sorted);

    return NJS_OK;
}


static njs_int_t
njs_mp_test_verify_all(njs_mp_test_item_t *items, njs_uint_t n)
{
    njs_uint_t  i;

    if (njs_mp_test_disjoint(items, n) != NJS_OK) {
        return NJS_ERROR;
    }

    for (i = 0; i < n; i++) {
        if (njs_mp_test_verify(items, i) != NJS_OK) {
            return NJS_ERROR;
        }
    }

    return NJS_OK;
}


static void
njs_mp_test_shuffle(njs_mp_test_item_t *items, njs_uint_t n, uint32_t *state)
{
    njs_uint_t          i, j;
    njs_mp_test_item_t  tmp;

    for (i = n - 1; i > 0; i--) {
        j = njs_mp_test_rand(state) % (i + 1);
        tmp = items[i]; items[i] = items[j]; items[j] = tmp;
    }
}


static njs_mp_t *
njs_mp_test_create(const njs_mp_test_conf_t *conf)
{
    njs_mp_t  *mp;

    mp = njs_mp_create(conf->cluster_size, conf->page_alignment,
                       conf->page_size, conf->min_chunk_size);
    if (mp == NULL) {
        njs_printf("mp test: njs_mp_create(%uz, %uz, %uz, %uz) failed\n",
                   conf->cluster_size, conf->page_alignment, conf->page_size,
                   conf->min_chunk_size);
    }

    return mp;
}


static njs_int_t
njs_mp_test_done(njs_mp_t *mp)
{
    njs_int_t  ret;

    ret = NJS_OK;

    if (!njs_mp_is_empty(mp)) {
        njs_printf("mp test: pool is not empty\n");
        ret = NJS_ERROR;
    }

    njs_mp_destroy(mp);

    return ret;
}


/*
 * Allocate n blocks of a random size out of [min, max], check that they
 * do not overlap and are not corrupted, then free them in the given
 * order.
 */

static njs_int_t
njs_mp_test_blocks(const njs_mp_test_conf_t *conf, njs_uint_t n, size_t min,
    size_t max, njs_mp_test_order_t order)
{
    size_t              size;
    uint32_t            state;
    njs_mp_t            *mp;
    njs_uint_t          i;
    njs_mp_stat_t       stat;
    njs_mp_test_item_t  *items;

    mp = njs_mp_test_create(conf);
    if (mp == NULL) {
        return NJS_ERROR;
    }

    items = malloc(n * sizeof(njs_mp_test_item_t));
    if (items == NULL) {
        njs_mp_destroy(mp);
        return NJS_ERROR;
    }

    state = 0x5A5A5A5A;

    for (i = 0; i < n; i++) {
        size = min + njs_mp_test_rand(&state) % (max - min + 1);

        if (njs_mp_test_fill(items, i, njs_mp_alloc(mp, size), size)
            != NJS_OK)
        {
            goto failed;
        }
    }

    if (njs_mp_test_disjoint(items, n) != NJS_OK) {
        goto failed;
    }

    njs_mp_stat(mp, &stat);

    if (stat.nblocks == 0 || stat.size == 0) {
        njs_printf("mp test: njs_mp_stat() reports an empty pool\n");
        goto failed;
    }

    switch (order) {
    case NJS_MP_TEST_REVERSE:
        for (i = 0; i < n / 2; i++) {
            njs_mp_test_item_t  tmp;

            tmp = items[i];
            items[i] = items[n - 1 - i];
            items[n - 1 - i] = tmp;
        }

        break;

    case NJS_MP_TEST_RANDOM:
        njs_mp_test_shuffle(items, n, &state);
        break;

    default:
        break;
    }

    for (i = 0; i < n; i++) {
        if (njs_mp_test_verify(items, i) != NJS_OK) {
            goto failed;
        }

        njs_mp_free(mp, (void *) items[i].start);
    }

    free(items);

    return njs_mp_test_done(mp);

failed:

    free(items);
    njs_mp_destroy(mp);

    return NJS_ERROR;
}


/*
 * Keep n blocks live and repeatedly free and reallocate a random one.
 * This releases and reuses whole pages and whole clusters.
 */

static njs_int_t
njs_mp_test_churn(const njs_mp_test_conf_t *conf, njs_uint_t n,
    njs_uint_t iters, size_t max)
{
    size_t              size;
    uint32_t            state;
    njs_mp_t            *mp;
    njs_uint_t          i, j;
    njs_mp_test_item_t  *items;

    mp = njs_mp_test_create(conf);
    if (mp == NULL) {
        return NJS_ERROR;
    }

    items = malloc(n * sizeof(njs_mp_test_item_t));
    if (items == NULL) {
        njs_mp_destroy(mp);
        return NJS_ERROR;
    }

    state = 0xA5A5A5A5;

    for (i = 0; i < n; i++) {
        size = 1 + njs_mp_test_rand(&state) % max;

        if (njs_mp_test_fill(items, i, njs_mp_alloc(mp, size), size)
            != NJS_OK)
        {
            goto failed;
        }
    }

    for (i = 0; i < iters; i++) {
        j = njs_mp_test_rand(&state) % n;

        if (njs_mp_test_verify(items, j) != NJS_OK) {
            goto failed;
        }

        njs_mp_free(mp, (void *) items[j].start);

        size = 1 + njs_mp_test_rand(&state) % max;

        if (njs_mp_test_fill(items, j, njs_mp_alloc(mp, size), size)
            != NJS_OK)
        {
            goto failed;
        }

        if ((i & 1023) == 1023
            && njs_mp_test_verify_all(items, n) != NJS_OK)
        {
            njs_printf("mp test: churn failed at iteration %uz, seed 0x%08x\n",
                       (size_t) i, state);
            goto failed;
        }
    }

    if (njs_mp_test_verify_all(items, n) != NJS_OK) {
        goto failed;
    }

    for (i = 0; i < n; i++) {
        if (njs_mp_test_verify(items, i) != NJS_OK) {
            goto failed;
        }

        njs_mp_free(mp, (void *) items[i].start);
    }

    free(items);

    return njs_mp_test_done(mp);

failed:

    free(items);
    njs_mp_destroy(mp);

    return NJS_ERROR;
}


/* One allocation of every size around the chunk and page edges. */

static njs_int_t
njs_mp_test_edges(const njs_mp_test_conf_t *conf)
{
    size_t              size, max;
    njs_mp_t            *mp;
    njs_uint_t          i, n;
    njs_mp_test_item_t  *items;

    max = conf->page_size * 2 + 1;

    mp = njs_mp_test_create(conf);
    if (mp == NULL) {
        return NJS_ERROR;
    }

    items = malloc(max * sizeof(njs_mp_test_item_t));
    if (items == NULL) {
        njs_mp_destroy(mp);
        return NJS_ERROR;
    }

    n = 0;

    for (size = 1; size <= max; size++) {
        if (njs_mp_test_fill(items, n, njs_mp_alloc(mp, size), size)
            != NJS_OK)
        {
            goto failed;
        }

        n++;
    }

    if (njs_mp_test_disjoint(items, n) != NJS_OK) {
        goto failed;
    }

    for (i = 0; i < n; i++) {
        if (njs_mp_test_verify(items, i) != NJS_OK) {
            goto failed;
        }

        njs_mp_free(mp, (void *) items[i].start);
    }

    free(items);

    return njs_mp_test_done(mp);

failed:

    free(items);
    njs_mp_destroy(mp);

    return NJS_ERROR;
}


static njs_int_t
njs_mp_test_align(const njs_mp_test_conf_t *conf)
{
    size_t              alignment, size;
    njs_mp_t            *mp;
    njs_uint_t          i, n;
    njs_mp_test_item_t  items[128];

    mp = njs_mp_test_create(conf);
    if (mp == NULL) {
        return NJS_ERROR;
    }

    n = 0;

    /*
     * The alignment starts at a pointer size: an allocation that does not
     * fit a page is served by njs_memalign(), which does not accept less,
     * and with --debug-memory=YES that is every allocation.
     */

    for (alignment = sizeof(void *); alignment <= 4096; alignment *= 2) {
        for (size = 1; size <= conf->page_size * 2; size *= 4) {
            if (n >= njs_nitems(items)) {
                njs_printf("mp test: too many align cases\n");
                goto failed;
            }

            if (njs_mp_test_fill(items, n, njs_mp_align(mp, alignment, size),
                                 size)
                != NJS_OK)
            {
                goto failed;
            }

            if (((uintptr_t) items[n].start & (alignment - 1)) != 0) {
                njs_printf("mp test: %p is not aligned to %uz\n",
                           (void *) items[n].start, alignment);
                goto failed;
            }

            n++;
        }
    }

    if (njs_mp_test_disjoint(items, n) != NJS_OK) {
        goto failed;
    }

    for (i = 0; i < n; i++) {
        if (njs_mp_test_verify(items, i) != NJS_OK) {
            goto failed;
        }

        njs_mp_free(mp, (void *) items[i].start);
    }

    /* njs_mp_free() must tolerate NULL. */
    njs_mp_free(mp, NULL);

    return njs_mp_test_done(mp);

failed:

    njs_mp_destroy(mp);

    return NJS_ERROR;
}


static njs_int_t
njs_mp_unit_test(njs_uint_t n)
{
    size_t                    page;
    njs_uint_t                i;
    const char                *stage;
    njs_mp_test_conf_t        conf;

    njs_printf("mp unit test started: %l blocks\n", (long) n);

    for (i = 0; i < njs_nitems(njs_mp_test_confs); i++) {
        conf = njs_mp_test_confs[i];
        page = conf.page_size;

        stage = "edges";
        if (njs_mp_test_edges(&conf) != NJS_OK) {
            goto failed;
        }

        stage = "alignment";
        if (njs_mp_test_align(&conf) != NJS_OK) {
            goto failed;
        }

        /* Chunks only. */

        stage = "chunks forward";
        if (njs_mp_test_blocks(&conf, n, 1, page / 2, NJS_MP_TEST_FORWARD)
            != NJS_OK)
        {
            goto failed;
        }

        stage = "chunks reverse";
        if (njs_mp_test_blocks(&conf, n, 1, page / 2, NJS_MP_TEST_REVERSE)
            != NJS_OK)
        {
            goto failed;
        }

        stage = "chunks random";
        if (njs_mp_test_blocks(&conf, n, 1, page / 2, NJS_MP_TEST_RANDOM)
            != NJS_OK)
        {
            goto failed;
        }

        /* Allocations just over a page are large blocks of their own. */

        stage = "large blocks";
        if (njs_mp_test_blocks(&conf, n, page + 1, page + page / 4,
                               NJS_MP_TEST_RANDOM)
            != NJS_OK)
        {
            goto failed;
        }

        /* Chunks, whole pages and large blocks at the same time. */

        stage = "mixed blocks";
        if (njs_mp_test_blocks(&conf, n, 1, page * 4, NJS_MP_TEST_RANDOM)
            != NJS_OK)
        {
            goto failed;
        }

        stage = "churn";
        if (njs_mp_test_churn(&conf, n / 4, n, page * 2) != NJS_OK) {
            goto failed;
        }
    }

    conf.cluster_size = 2 * njs_pagesize();
    conf.page_alignment = 128;
    conf.page_size = 512;
    conf.min_chunk_size = 16;

    stage = "runtime configuration";
    if (njs_mp_test_churn(&conf, n / 4, n, conf.page_size * 2) != NJS_OK) {
        goto failed;
    }

    njs_printf("mp unit test passed\n");

    return NJS_OK;

failed:

    njs_printf("mp test failed: %s, cluster %uz, alignment %uz, page %uz, "
               "chunk %uz\n", stage, conf.cluster_size, conf.page_alignment,
               conf.page_size, conf.min_chunk_size);

    return NJS_ERROR;
}


int
main(void)
{
    return njs_mp_unit_test(4000) != NJS_OK;
}
