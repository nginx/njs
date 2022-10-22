
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


/*
 * A memory cache pool allocates memory in clusters of specified size and
 * aligned to page_alignment.  A cluster is divided on pages of specified
 * size.  Page size must be a power of 2.  A page can be used entirely or
 * can be divided on chunks of equal size.  Chunk size must be a power of 2.
 * A cluster can contains pages with different chunk sizes.  Cluster size
 * must be a multiple of page size and may be not a power of 2.  Allocations
 * greater than page are allocated outside clusters.  Start addresses and
 * sizes of the clusters and large allocations are stored in rbtree blocks
 * to find them on free operations.  The rbtree nodes are sorted by start
 * addresses.
 */


typedef struct {
    /*
     * Used to link pages with free chunks in pool chunk slot list
     * or to link free pages in clusters.
     */
    njs_queue_link_t            link;

    /*
     * Size of chunks or page shifted by mp->chunk_size_shift.
     * Zero means that page is free.
     */
    uint8_t                     size;

    /*
     * Page number in page cluster.
     * There can be no more than 256 pages in a cluster.
     */
    uint8_t                     number;

    /* Number of free chunks of a chunked page. */
    uint8_t                     chunks;

    uint8_t                     _unused;

    /* Chunk bitmap.  There can be no more than 32 chunks in a page. */
    uint8_t                     map[4];
} njs_mp_page_t;


typedef enum {
    /* Block of cluster.  The block is allocated apart of the cluster. */
    NJS_MP_CLUSTER_BLOCK = 0,
    /*
     * Block of large allocation.
     * The block is allocated apart of the allocation.
     */
    NJS_MP_DISCRETE_BLOCK,
    /*
     * Block of large allocation.
     * The block is allocated just after of the allocation.
     */
    NJS_MP_EMBEDDED_BLOCK,
} njs_mp_block_type_t;


typedef struct {
    NJS_RBTREE_NODE             (node);
    njs_mp_block_type_t         type:8;

    /* Block size must be less than 4G. */
    uint32_t                    size;

    u_char                      *start;
    njs_mp_page_t               pages[];
} njs_mp_block_t;


typedef struct {
    njs_queue_t                 pages;

    /* Size of page chunks. */
#if (NJS_64BIT)
    uint32_t                    size;
#else
    uint16_t                    size;
#endif

    /* Maximum number of free chunks in chunked page. */
    uint8_t                     chunks;
} njs_mp_slot_t;


struct njs_mp_s {
    /* rbtree of njs_mp_block_t. */
    njs_rbtree_t                blocks;

    njs_queue_t                 free_pages;

    uint8_t                     chunk_size_shift;
    uint8_t                     page_size_shift;
    uint32_t                    page_size;
    uint32_t                    page_alignment;
    uint32_t                    cluster_size;

    njs_mp_cleanup_t            *cleanup;

    njs_mp_slot_t               slots[];
};


#define njs_mp_chunk_is_free(map, chunk)                                      \
    ((map[chunk / 8] & (0x80 >> (chunk & 7))) == 0)


#define njs_mp_chunk_set_free(map, chunk)                                     \
    map[chunk / 8] &= ~(0x80 >> (chunk & 7))


#define njs_mp_free_junk(p, size)                                             \
    njs_memset((p), 0x5A, size)


#define njs_is_power_of_two(value)                                            \
    ((((value) - 1) & (value)) == 0)


static njs_uint_t njs_mp_shift(njs_uint_t n);
#if !(NJS_DEBUG_MEMORY)
static void *njs_mp_alloc_small(njs_mp_t *mp, size_t size);
static njs_uint_t njs_mp_alloc_chunk(u_char *map, njs_uint_t size);
static njs_mp_page_t *njs_mp_alloc_page(njs_mp_t *mp);
static njs_mp_block_t *njs_mp_alloc_cluster(njs_mp_t *mp);
#endif
static void *njs_mp_alloc_large(njs_mp_t *mp, size_t alignment, size_t size);
static intptr_t njs_mp_rbtree_compare(njs_rbtree_node_t *node1,
    njs_rbtree_node_t *node2);
static njs_mp_block_t *njs_mp_find_block(njs_rbtree_t *tree,
    u_char *p);
static const char *njs_mp_chunk_free(njs_mp_t *mp, njs_mp_block_t *cluster,
    u_char *p);


njs_mp_t *
njs_mp_create(size_t cluster_size, size_t page_alignment, size_t page_size,
    size_t min_chunk_size)
{
    /* Alignment and sizes must be a power of 2. */

    if (njs_slow_path(!njs_is_power_of_two(page_alignment)
                     || !njs_is_power_of_two(page_size)
                     || !njs_is_power_of_two(min_chunk_size)))
    {
        return NULL;
    }

    page_alignment = njs_max(page_alignment, NJS_MAX_ALIGNMENT);

    if (njs_slow_path(page_size < 64
                     || page_size < page_alignment
                     || page_size < min_chunk_size
                     || min_chunk_size * 32 < page_size
                     || cluster_size < page_size
                     || cluster_size / page_size > 256
                     || cluster_size % page_size != 0))
    {
        return NULL;
    }

    return njs_mp_fast_create(cluster_size, page_alignment, page_size,
                              min_chunk_size);
}


njs_mp_t *
njs_mp_fast_create(size_t cluster_size, size_t page_alignment, size_t page_size,
    size_t min_chunk_size)
{
    njs_mp_t       *mp;
    njs_uint_t     slots, chunk_size;
    njs_mp_slot_t  *slot;

    slots = 0;
    chunk_size = page_size;

    do {
        slots++;
        chunk_size /= 2;
    } while (chunk_size > min_chunk_size);

    mp = njs_zalloc(sizeof(njs_mp_t) + slots * sizeof(njs_mp_slot_t));

    if (njs_fast_path(mp != NULL)) {
        mp->page_size = page_size;
        mp->page_alignment = njs_max(page_alignment, NJS_MAX_ALIGNMENT);
        mp->cluster_size = cluster_size;

        slot = mp->slots;

        do {
            njs_queue_init(&slot->pages);

            slot->size = chunk_size;
            /* slot->chunks should be one less than actual number of chunks. */
            slot->chunks = (page_size / chunk_size) - 1;

            slot++;
            chunk_size *= 2;
        } while (chunk_size < page_size);

        mp->chunk_size_shift = njs_mp_shift(min_chunk_size);
        mp->page_size_shift = njs_mp_shift(page_size);

        njs_rbtree_init(&mp->blocks, njs_mp_rbtree_compare);

        njs_queue_init(&mp->free_pages);
    }

    return mp;
}


static njs_uint_t
njs_mp_shift(njs_uint_t n)
{
    njs_uint_t  shift;

    shift = 0;
    n /= 2;

    do {
        shift++;
        n /= 2;
    } while (n != 0);

    return shift;
}


njs_bool_t
njs_mp_is_empty(njs_mp_t *mp)
{
    return (njs_rbtree_is_empty(&mp->blocks)
            && njs_queue_is_empty(&mp->free_pages));
}


void
njs_mp_destroy(njs_mp_t *mp)
{
    void               *p;
    njs_mp_block_t     *block;
    njs_mp_cleanup_t   *c;
    njs_rbtree_node_t  *node, *next;

    njs_debug_alloc("mp destroy\n");

    for (c = mp->cleanup; c != NULL; c = c->next) {
        if (c->handler != NULL) {
            njs_debug_alloc("mp run cleanup: @%p\n", c);
            c->handler(c->data);
        }
    }

    next = njs_rbtree_root(&mp->blocks);

    while (next != njs_rbtree_sentinel(&mp->blocks)) {

        node = njs_rbtree_destroy_next(&mp->blocks, &next);
        block = (njs_mp_block_t *) node;

        p = block->start;

        if (block->type != NJS_MP_EMBEDDED_BLOCK) {
            njs_free(block);
        }

        njs_free(p);
    }

    njs_free(mp);
}


void
njs_mp_stat(njs_mp_t *mp, njs_mp_stat_t *stat)
{
    njs_mp_block_t     *block;
    njs_rbtree_node_t  *node;

    stat->size = 0;
    stat->nblocks = 0;
    stat->cluster_size = mp->cluster_size;
    stat->page_size = mp->page_size;

    node = njs_rbtree_min(&mp->blocks);

    while (njs_rbtree_is_there_successor(&mp->blocks, node)) {
        block = (njs_mp_block_t *) node;

        stat->nblocks++;
        stat->size += block->size;

        node = njs_rbtree_node_successor(&mp->blocks, node);
    }
}


void *
njs_mp_alloc(njs_mp_t *mp, size_t size)
{
    njs_debug_alloc("mp alloc: %uz\n", size);

#if !(NJS_DEBUG_MEMORY)

    if (size <= mp->page_size) {
        return njs_mp_alloc_small(mp, size);
    }

#endif

    return njs_mp_alloc_large(mp, NJS_MAX_ALIGNMENT, size);
}


void *
njs_mp_zalloc(njs_mp_t *mp, size_t size)
{
    void  *p;

    p = njs_mp_alloc(mp, size);

    if (njs_fast_path(p != NULL)) {
        njs_memzero(p, size);
    }

    return p;
}


void *
njs_mp_align(njs_mp_t *mp, size_t alignment, size_t size)
{
    njs_debug_alloc("mp align: @%uz:%uz\n", alignment, size);

    /* Alignment must be a power of 2. */

    if (njs_fast_path(njs_is_power_of_two(alignment))) {

#if !(NJS_DEBUG_MEMORY)

        if (size <= mp->page_size && alignment <= mp->page_alignment) {
            size = njs_max(size, alignment);

            if (size <= mp->page_size) {
                return njs_mp_alloc_small(mp, size);
            }
        }

#endif

        return njs_mp_alloc_large(mp, alignment, size);
    }

    return NULL;
}


void *
njs_mp_zalign(njs_mp_t *mp, size_t alignment, size_t size)
{
    void  *p;

    p = njs_mp_align(mp, alignment, size);

    if (njs_fast_path(p != NULL)) {
        njs_memzero(p, size);
    }

    return p;
}


#if !(NJS_DEBUG_MEMORY)

njs_inline u_char *
njs_mp_page_addr(njs_mp_t *mp, njs_mp_page_t *page)
{
    njs_mp_block_t  *block;

    block = (njs_mp_block_t *)
                ((u_char *) page - page->number * sizeof(njs_mp_page_t)
                 - offsetof(njs_mp_block_t, pages));

    return block->start + (page->number << mp->page_size_shift);
}


static void *
njs_mp_alloc_small(njs_mp_t *mp, size_t size)
{
    u_char            *p;
    njs_mp_page_t     *page;
    njs_mp_slot_t     *slot;
    njs_queue_link_t  *link;

    p = NULL;

    if (size <= mp->page_size / 2) {

        /* Find a slot with appropriate chunk size. */
        for (slot = mp->slots; slot->size < size; slot++) { /* void */ }

        size = slot->size;

        if (njs_fast_path(!njs_queue_is_empty(&slot->pages))) {

            link = njs_queue_first(&slot->pages);
            page = njs_queue_link_data(link, njs_mp_page_t, link);

            p = njs_mp_page_addr(mp, page);
            p += njs_mp_alloc_chunk(page->map, size);

            page->chunks--;

            if (page->chunks == 0) {
                /*
                 * Remove full page from the mp chunk slot list
                 * of pages with free chunks.
                 */
                njs_queue_remove(&page->link);
            }

        } else {
            page = njs_mp_alloc_page(mp);

            if (njs_fast_path(page != NULL)) {

                njs_queue_insert_head(&slot->pages, &page->link);

                /* Mark the first chunk as busy. */
                page->map[0] = 0x80;
                page->map[1] = 0;
                page->map[2] = 0;
                page->map[3] = 0;

                /* slot->chunks are already one less. */
                page->chunks = slot->chunks;
                page->size = size >> mp->chunk_size_shift;

                p = njs_mp_page_addr(mp, page);
            }
        }

    } else {
        page = njs_mp_alloc_page(mp);

        if (njs_fast_path(page != NULL)) {
            page->size = mp->page_size >> mp->chunk_size_shift;

            p = njs_mp_page_addr(mp, page);
        }

#if (NJS_DEBUG)
        size = mp->page_size;
#endif
    }

    njs_debug_alloc("mp chunk:%uz alloc: %p\n", size, p);

    return p;
}


static njs_uint_t
njs_mp_alloc_chunk(uint8_t *map, njs_uint_t size)
{
    uint8_t     mask;
    njs_uint_t  n, offset;

    offset = 0;
    n = 0;

    /* The page must have at least one free chunk. */

    for ( ;; ) {
        if (map[n] != 0xff) {

            mask = 0x80;

            do {
                if ((map[n] & mask) == 0) {
                    /* A free chunk is found. */
                    map[n] |= mask;
                    return offset;
                }

                offset += size;
                mask >>= 1;

            } while (mask != 0);

        } else {
            /* Fast-forward: all 8 chunks are occupied. */
            offset += size * 8;
        }

        n++;
    }
}


static njs_mp_page_t *
njs_mp_alloc_page(njs_mp_t *mp)
{
    njs_mp_page_t     *page;
    njs_mp_block_t    *cluster;
    njs_queue_link_t  *link;

    if (njs_queue_is_empty(&mp->free_pages)) {
        cluster = njs_mp_alloc_cluster(mp);
        if (njs_slow_path(cluster == NULL)) {
            return NULL;
        }
    }

    link = njs_queue_first(&mp->free_pages);
    njs_queue_remove(link);

    page = njs_queue_link_data(link, njs_mp_page_t, link);

    return page;
}


static njs_mp_block_t *
njs_mp_alloc_cluster(njs_mp_t *mp)
{
    njs_uint_t      n;
    njs_mp_block_t  *cluster;

    n = mp->cluster_size >> mp->page_size_shift;

    cluster = njs_zalloc(sizeof(njs_mp_block_t) + n * sizeof(njs_mp_page_t));

    if (njs_slow_path(cluster == NULL)) {
        return NULL;
    }

    /* NJS_MP_CLUSTER_BLOCK type is zero. */

    cluster->size = mp->cluster_size;

    cluster->start = njs_memalign(mp->page_alignment, mp->cluster_size);
    if (njs_slow_path(cluster->start == NULL)) {
        njs_free(cluster);
        return NULL;
    }

    n--;
    cluster->pages[n].number = n;
    njs_queue_insert_head(&mp->free_pages, &cluster->pages[n].link);

    while (n != 0) {
        n--;
        cluster->pages[n].number = n;
        njs_queue_insert_before(&cluster->pages[n + 1].link,
                                &cluster->pages[n].link);
    }

    njs_rbtree_insert(&mp->blocks, &cluster->node);

    return cluster;
}

#endif


static void *
njs_mp_alloc_large(njs_mp_t *mp, size_t alignment, size_t size)
{
    u_char          *p;
    size_t          aligned_size;
    uint8_t         type;
    njs_mp_block_t  *block;

    /* Allocation must be less than 4G. */
    if (njs_slow_path(size >= UINT32_MAX)) {
        return NULL;
    }

    if (njs_is_power_of_two(size)) {
        block = njs_malloc(sizeof(njs_mp_block_t));
        if (njs_slow_path(block == NULL)) {
            return NULL;
        }

        p = njs_memalign(alignment, size);
        if (njs_slow_path(p == NULL)) {
            njs_free(block);
            return NULL;
        }

        type = NJS_MP_DISCRETE_BLOCK;

    } else {
        aligned_size = njs_align_size(size, sizeof(uintptr_t));

        p = njs_memalign(alignment, aligned_size + sizeof(njs_mp_block_t));
        if (njs_slow_path(p == NULL)) {
            return NULL;
        }

        block = (njs_mp_block_t *) (p + aligned_size);
        type = NJS_MP_EMBEDDED_BLOCK;
    }

    block->type = type;
    block->size = size;
    block->start = p;

    njs_rbtree_insert(&mp->blocks, &block->node);

    return p;
}


static intptr_t
njs_mp_rbtree_compare(njs_rbtree_node_t *node1, njs_rbtree_node_t *node2)
{
    njs_mp_block_t  *block1, *block2;

    block1 = (njs_mp_block_t *) node1;
    block2 = (njs_mp_block_t *) node2;

    return (uintptr_t) block1->start - (uintptr_t) block2->start;
}


njs_mp_cleanup_t *
njs_mp_cleanup_add(njs_mp_t *mp, size_t size)
{
    njs_mp_cleanup_t  *c;

    c = njs_mp_alloc(mp, sizeof(njs_mp_cleanup_t));
    if (njs_slow_path(c == NULL)) {
        return NULL;
    }

    if (size) {
        c->data = njs_mp_alloc(mp, size);
        if (njs_slow_path(c->data == NULL)) {
            return NULL;
        }

    } else {
        c->data = NULL;
    }

    c->handler = NULL;
    c->next = mp->cleanup;

    mp->cleanup = c;

    njs_debug_alloc("mp add cleanup: @%p\n", c);

    return c;
}


void
njs_mp_free(njs_mp_t *mp, void *p)
{
    const char      *err;
    njs_mp_block_t  *block;

    njs_debug_alloc("mp free: @%p\n", p);

    block = njs_mp_find_block(&mp->blocks, p);

    if (njs_fast_path(block != NULL)) {

        if (block->type == NJS_MP_CLUSTER_BLOCK) {
            err = njs_mp_chunk_free(mp, block, p);

            if (njs_fast_path(err == NULL)) {
                return;
            }

        } else if (njs_fast_path(p == block->start)) {
            njs_rbtree_delete(&mp->blocks, &block->node);

            if (block->type == NJS_MP_DISCRETE_BLOCK) {
                njs_free(block);
            }

            njs_free(p);

            return;

        } else {
            njs_assert_msg(0, "freed pointer points to middle of blk: %p\n", p);
        }

    } else {
        njs_assert_msg(p == NULL, "freed pointer is out of mp: %p\n", p);
    }
}


static njs_mp_block_t *
njs_mp_find_block(njs_rbtree_t *tree, u_char *p)
{
    njs_mp_block_t     *block;
    njs_rbtree_node_t  *node, *sentinel;

    node = njs_rbtree_root(tree);
    sentinel = njs_rbtree_sentinel(tree);

    while (node != sentinel) {

        block = (njs_mp_block_t *) node;

        if (p < block->start) {
            node = node->left;

        } else if (p >= block->start + block->size) {
            node = node->right;

        } else {
            return block;
        }
    }

    return NULL;
}


static const char *
njs_mp_chunk_free(njs_mp_t *mp, njs_mp_block_t *cluster,
    u_char *p)
{
    u_char         *start;
    uintptr_t      offset;
    njs_uint_t     n, size, chunk;
    njs_mp_page_t  *page;
    njs_mp_slot_t  *slot;

    n = (p - cluster->start) >> mp->page_size_shift;
    start = cluster->start + (n << mp->page_size_shift);

    page = &cluster->pages[n];

    if (page->size == 0) {
        return "freed pointer points to already free page: %p";
    }

    size = page->size << mp->chunk_size_shift;

    if (size != mp->page_size) {

        offset = (uintptr_t) (p - start) & (mp->page_size - 1);
        chunk = offset / size;

        if (njs_slow_path(offset != chunk * size)) {
            return "freed pointer points to wrong chunk: %p";
        }

        if (njs_slow_path(njs_mp_chunk_is_free(page->map, chunk))) {
            return "freed pointer points to already free chunk: %p";
        }

        njs_mp_chunk_set_free(page->map, chunk);

        /* Find a slot with appropriate chunk size. */
        for (slot = mp->slots; slot->size < size; slot++) { /* void */ }

        if (page->chunks != slot->chunks) {
            page->chunks++;

            if (page->chunks == 1) {
                /*
                 * Add the page to the head of mp chunk slot list
                 * of pages with free chunks.
                 */
                njs_queue_insert_head(&slot->pages, &page->link);
            }

            njs_mp_free_junk(p, size);

            return NULL;

        } else {
            /*
             * All chunks are free, remove the page from mp chunk slot
             * list of pages with free chunks.
             */
            njs_queue_remove(&page->link);
        }

    } else if (njs_slow_path(p != start)) {
        return "invalid pointer to chunk: %p";
    }

    /* Add the free page to the mp's free pages tree. */

    page->size = 0;
    njs_queue_insert_head(&mp->free_pages, &page->link);

    njs_mp_free_junk(p, size);

    /* Test if all pages in the cluster are free. */

    page = cluster->pages;
    n = mp->cluster_size >> mp->page_size_shift;

    do {
         if (page->size != 0) {
             return NULL;
         }

         page++;
         n--;
    } while (n != 0);

    /* Free cluster. */

    page = cluster->pages;
    n = mp->cluster_size >> mp->page_size_shift;

    do {
         njs_queue_remove(&page->link);
         page++;
         n--;
    } while (n != 0);

    njs_rbtree_delete(&mp->blocks, &cluster->node);

    p = cluster->start;

    njs_free(cluster);
    njs_free(p);

    return NULL;
}
