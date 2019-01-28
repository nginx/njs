
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_alignment.h>
#include <nxt_stub.h>
#include <nxt_string.h>
#include <nxt_queue.h>
#include <nxt_rbtree.h>
#include <nxt_mp.h>
#include <string.h>
#include <stdint.h>


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
    nxt_queue_link_t            link;

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
} nxt_mp_page_t;


typedef enum {
    /* Block of cluster.  The block is allocated apart of the cluster. */
    NXT_MP_CLUSTER_BLOCK = 0,
    /*
     * Block of large allocation.
     * The block is allocated apart of the allocation.
     */
    NXT_MP_DISCRETE_BLOCK,
    /*
     * Block of large allocation.
     * The block is allocated just after of the allocation.
     */
    NXT_MP_EMBEDDED_BLOCK,
} nxt_mp_block_type_t;


typedef struct {
    NXT_RBTREE_NODE             (node);
    nxt_mp_block_type_t         type:8;

    /* Block size must be less than 4G. */
    uint32_t                    size;

    u_char                      *start;
    nxt_mp_page_t               pages[];
} nxt_mp_block_t;


typedef struct {
    nxt_queue_t                 pages;

    /* Size of page chunks. */
#if (NXT_64BIT)
    uint32_t                    size;
#else
    uint16_t                    size;
#endif

    /* Maximum number of free chunks in chunked page. */
    uint8_t                     chunks;
} nxt_mp_slot_t;


struct nxt_mp_s {
    /* rbtree of nxt_mp_block_t. */
    nxt_rbtree_t                blocks;

    nxt_queue_t                 free_pages;

    uint8_t                     chunk_size_shift;
    uint8_t                     page_size_shift;
    uint32_t                    page_size;
    uint32_t                    page_alignment;
    uint32_t                    cluster_size;

    const nxt_mem_proto_t       *proto;
    void                        *mem;
    void                        *trace;

    nxt_mp_slot_t               slots[];
};


#define nxt_mp_chunk_is_free(map, chunk)                                      \
    ((map[chunk / 8] & (0x80 >> (chunk & 7))) == 0)


#define nxt_mp_chunk_set_free(map, chunk)                                     \
    map[chunk / 8] &= ~(0x80 >> (chunk & 7))


#define nxt_mp_free_junk(p, size)                                             \
    nxt_memset((p), 0x5A, size)


#define nxt_is_power_of_two(value)                                            \
    ((((value) - 1) & (value)) == 0)


static nxt_uint_t nxt_mp_shift(nxt_uint_t n);
#if !(NXT_DEBUG_MEMORY)
static void *nxt_mp_alloc_small(nxt_mp_t *mp, size_t size);
static nxt_uint_t nxt_mp_alloc_chunk(u_char *map, nxt_uint_t size);
static nxt_mp_page_t *nxt_mp_alloc_page(nxt_mp_t *mp);
static nxt_mp_block_t *nxt_mp_alloc_cluster(nxt_mp_t *mp);
#endif
static void *nxt_mp_alloc_large(nxt_mp_t *mp, size_t alignment, size_t size);
static intptr_t nxt_mp_rbtree_compare(nxt_rbtree_node_t *node1,
    nxt_rbtree_node_t *node2);
static nxt_mp_block_t *nxt_mp_find_block(nxt_rbtree_t *tree,
    u_char *p);
static const char *nxt_mp_chunk_free(nxt_mp_t *mp, nxt_mp_block_t *cluster,
    u_char *p);


nxt_mp_t *
nxt_mp_create(const nxt_mem_proto_t *proto, void *mem,
    void *trace, size_t cluster_size, size_t page_alignment, size_t page_size,
    size_t min_chunk_size)
{
    /* Alignment and sizes must be a power of 2. */

    if (nxt_slow_path(!nxt_is_power_of_two(page_alignment)
                     || !nxt_is_power_of_two(page_size)
                     || !nxt_is_power_of_two(min_chunk_size)))
    {
        return NULL;
    }

    page_alignment = nxt_max(page_alignment, NXT_MAX_ALIGNMENT);

    if (nxt_slow_path(page_size < 64
                     || page_size < page_alignment
                     || page_size < min_chunk_size
                     || min_chunk_size * 32 < page_size
                     || cluster_size < page_size
                     || cluster_size / page_size > 256
                     || cluster_size % page_size != 0))
    {
        return NULL;
    }

    return nxt_mp_fast_create(proto, mem, trace, cluster_size, page_alignment,
                              page_size, min_chunk_size);
}


nxt_mp_t *
nxt_mp_fast_create(const nxt_mem_proto_t *proto, void *mem,
    void *trace, size_t cluster_size, size_t page_alignment, size_t page_size,
    size_t min_chunk_size)
{
    nxt_mp_t       *mp;
    nxt_uint_t     slots, chunk_size;
    nxt_mp_slot_t  *slot;

    slots = 0;
    chunk_size = page_size;

    do {
        slots++;
        chunk_size /= 2;
    } while (chunk_size > min_chunk_size);

    mp = proto->zalloc(mem, sizeof(nxt_mp_t) + slots * sizeof(nxt_mp_slot_t));

    if (nxt_fast_path(mp != NULL)) {
        mp->proto = proto;
        mp->mem = mem;
        mp->trace = trace;

        mp->page_size = page_size;
        mp->page_alignment = nxt_max(page_alignment, NXT_MAX_ALIGNMENT);
        mp->cluster_size = cluster_size;

        slot = mp->slots;

        do {
            nxt_queue_init(&slot->pages);

            slot->size = chunk_size;
            /* slot->chunks should be one less than actual number of chunks. */
            slot->chunks = (page_size / chunk_size) - 1;

            slot++;
            chunk_size *= 2;
        } while (chunk_size < page_size);

        mp->chunk_size_shift = nxt_mp_shift(min_chunk_size);
        mp->page_size_shift = nxt_mp_shift(page_size);

        nxt_rbtree_init(&mp->blocks, nxt_mp_rbtree_compare);

        nxt_queue_init(&mp->free_pages);
    }

    return mp;
}


static nxt_uint_t
nxt_mp_shift(nxt_uint_t n)
{
    nxt_uint_t  shift;

    shift = 0;
    n /= 2;

    do {
        shift++;
        n /= 2;
    } while (n != 0);

    return shift;
}


nxt_bool_t
nxt_mp_is_empty(nxt_mp_t *mp)
{
    return (nxt_rbtree_is_empty(&mp->blocks)
            && nxt_queue_is_empty(&mp->free_pages));
}


void
nxt_mp_destroy(nxt_mp_t *mp)
{
    void               *p;
    nxt_mp_block_t     *block;
    nxt_rbtree_node_t  *node, *next;

    next = nxt_rbtree_root(&mp->blocks);

    while (next != nxt_rbtree_sentinel(&mp->blocks)) {

        node = nxt_rbtree_destroy_next(&mp->blocks, &next);
        block = (nxt_mp_block_t *) node;

        p = block->start;

        if (block->type != NXT_MP_EMBEDDED_BLOCK) {
            mp->proto->free(mp->mem, block);
        }

        mp->proto->free(mp->mem, p);
    }

    mp->proto->free(mp->mem, mp);
}


void *
nxt_mp_alloc(nxt_mp_t *mp, size_t size)
{
    if (mp->proto->trace != NULL) {
        mp->proto->trace(mp->trace, "mem cache alloc: %zd", size);
    }

#if !(NXT_DEBUG_MEMORY)

    if (size <= mp->page_size) {
        return nxt_mp_alloc_small(mp, size);
    }

#endif

    return nxt_mp_alloc_large(mp, NXT_MAX_ALIGNMENT, size);
}


void *
nxt_mp_zalloc(nxt_mp_t *mp, size_t size)
{
    void  *p;

    p = nxt_mp_alloc(mp, size);

    if (nxt_fast_path(p != NULL)) {
        nxt_memzero(p, size);
    }

    return p;
}


void *
nxt_mp_align(nxt_mp_t *mp, size_t alignment, size_t size)
{
    if (mp->proto->trace != NULL) {
        mp->proto->trace(mp->trace,
                         "mem cache align: @%zd:%zd", alignment, size);
    }

    /* Alignment must be a power of 2. */

    if (nxt_fast_path(nxt_is_power_of_two(alignment))) {

#if !(NXT_DEBUG_MEMORY)

        if (size <= mp->page_size && alignment <= mp->page_alignment) {
            size = nxt_max(size, alignment);

            if (size <= mp->page_size) {
                return nxt_mp_alloc_small(mp, size);
            }
        }

#endif

        return nxt_mp_alloc_large(mp, alignment, size);
    }

    return NULL;
}


void *
nxt_mp_zalign(nxt_mp_t *mp, size_t alignment, size_t size)
{
    void  *p;

    p = nxt_mp_align(mp, alignment, size);

    if (nxt_fast_path(p != NULL)) {
        nxt_memzero(p, size);
    }

    return p;
}


#if !(NXT_DEBUG_MEMORY)

nxt_inline u_char *
nxt_mp_page_addr(nxt_mp_t *mp, nxt_mp_page_t *page)
{
    nxt_mp_block_t  *block;

    block = (nxt_mp_block_t *)
                ((u_char *) page - page->number * sizeof(nxt_mp_page_t)
                 - offsetof(nxt_mp_block_t, pages));

    return block->start + (page->number << mp->page_size_shift);
}


static void *
nxt_mp_alloc_small(nxt_mp_t *mp, size_t size)
{
    u_char            *p;
    nxt_mp_page_t     *page;
    nxt_mp_slot_t     *slot;
    nxt_queue_link_t  *link;

    p = NULL;

    if (size <= mp->page_size / 2) {

        /* Find a slot with appropriate chunk size. */
        for (slot = mp->slots; slot->size < size; slot++) { /* void */ }

        size = slot->size;

        if (nxt_fast_path(!nxt_queue_is_empty(&slot->pages))) {

            link = nxt_queue_first(&slot->pages);
            page = nxt_queue_link_data(link, nxt_mp_page_t, link);

            p = nxt_mp_page_addr(mp, page);
            p += nxt_mp_alloc_chunk(page->map, size);

            page->chunks--;

            if (page->chunks == 0) {
                /*
                 * Remove full page from the mp chunk slot list
                 * of pages with free chunks.
                 */
                nxt_queue_remove(&page->link);
            }

        } else {
            page = nxt_mp_alloc_page(mp);

            if (nxt_fast_path(page != NULL)) {

                nxt_queue_insert_head(&slot->pages, &page->link);

                /* Mark the first chunk as busy. */
                page->map[0] = 0x80;
                page->map[1] = 0;
                page->map[2] = 0;
                page->map[3] = 0;

                /* slot->chunks are already one less. */
                page->chunks = slot->chunks;
                page->size = size >> mp->chunk_size_shift;

                p = nxt_mp_page_addr(mp, page);
            }
        }

    } else {
        page = nxt_mp_alloc_page(mp);

        if (nxt_fast_path(page != NULL)) {
            page->size = mp->page_size >> mp->chunk_size_shift;

            p = nxt_mp_page_addr(mp, page);
        }

#if (NXT_DEBUG)
        size = mp->page_size;
#endif
    }

    if (mp->proto->trace != NULL) {
        mp->proto->trace(mp->trace, "mem cache chunk:%uz alloc: %p", size, p);
    }

    return p;
}


static nxt_uint_t
nxt_mp_alloc_chunk(uint8_t *map, nxt_uint_t size)
{
    uint8_t     mask;
    nxt_uint_t  n, offset;

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


static nxt_mp_page_t *
nxt_mp_alloc_page(nxt_mp_t *mp)
{
    nxt_mp_page_t     *page;
    nxt_mp_block_t    *cluster;
    nxt_queue_link_t  *link;

    if (nxt_queue_is_empty(&mp->free_pages)) {
        cluster = nxt_mp_alloc_cluster(mp);
        if (nxt_slow_path(cluster == NULL)) {
            return NULL;
        }
    }

    link = nxt_queue_first(&mp->free_pages);
    nxt_queue_remove(link);

    page = nxt_queue_link_data(link, nxt_mp_page_t, link);

    return page;
}


static nxt_mp_block_t *
nxt_mp_alloc_cluster(nxt_mp_t *mp)
{
    nxt_uint_t      n;
    nxt_mp_block_t  *cluster;

    n = mp->cluster_size >> mp->page_size_shift;

    cluster = mp->proto->zalloc(mp->mem,
                                sizeof(nxt_mp_block_t)
                                + n * sizeof(nxt_mp_page_t));

    if (nxt_slow_path(cluster == NULL)) {
        return NULL;
    }

    /* NXT_MP_CLUSTER_BLOCK type is zero. */

    cluster->size = mp->cluster_size;

    cluster->start = mp->proto->align(mp->mem, mp->page_alignment,
                                      mp->cluster_size);
    if (nxt_slow_path(cluster->start == NULL)) {
        mp->proto->free(mp->mem, cluster);
        return NULL;
    }

    n--;
    cluster->pages[n].number = n;
    nxt_queue_insert_head(&mp->free_pages, &cluster->pages[n].link);

    while (n != 0) {
        n--;
        cluster->pages[n].number = n;
        nxt_queue_insert_before(&cluster->pages[n + 1].link,
                                &cluster->pages[n].link);
    }

    nxt_rbtree_insert(&mp->blocks, &cluster->node);

    return cluster;
}

#endif


static void *
nxt_mp_alloc_large(nxt_mp_t *mp, size_t alignment, size_t size)
{
    u_char          *p;
    size_t          aligned_size;
    uint8_t         type;
    nxt_mp_block_t  *block;

    /* Allocation must be less than 4G. */
    if (nxt_slow_path(size >= UINT32_MAX)) {
        return NULL;
    }

    if (nxt_is_power_of_two(size)) {
        block = mp->proto->alloc(mp->mem, sizeof(nxt_mp_block_t));
        if (nxt_slow_path(block == NULL)) {
            return NULL;
        }

        p = mp->proto->align(mp->mem, alignment, size);
        if (nxt_slow_path(p == NULL)) {
            mp->proto->free(mp->mem, block);
            return NULL;
        }

        type = NXT_MP_DISCRETE_BLOCK;

    } else {
        aligned_size = nxt_align_size(size, sizeof(uintptr_t));

        p = mp->proto->align(mp->mem, alignment,
                             aligned_size + sizeof(nxt_mp_block_t));

        if (nxt_slow_path(p == NULL)) {
            return NULL;
        }

        block = (nxt_mp_block_t *) (p + aligned_size);
        type = NXT_MP_EMBEDDED_BLOCK;
    }

    block->type = type;
    block->size = size;
    block->start = p;

    nxt_rbtree_insert(&mp->blocks, &block->node);

    return p;
}


static intptr_t
nxt_mp_rbtree_compare(nxt_rbtree_node_t *node1, nxt_rbtree_node_t *node2)
{
    nxt_mp_block_t  *block1, *block2;

    block1 = (nxt_mp_block_t *) node1;
    block2 = (nxt_mp_block_t *) node2;

    return (uintptr_t) block1->start - (uintptr_t) block2->start;
}


void
nxt_mp_free(nxt_mp_t *mp, void *p)
{
    const char      *err;
    nxt_mp_block_t  *block;

    if (mp->proto->trace != NULL) {
        mp->proto->trace(mp->trace, "mem cache free %p", p);
    }

    block = nxt_mp_find_block(&mp->blocks, p);

    if (nxt_fast_path(block != NULL)) {

        if (block->type == NXT_MP_CLUSTER_BLOCK) {
            err = nxt_mp_chunk_free(mp, block, p);

            if (nxt_fast_path(err == NULL)) {
                return;
            }

        } else if (nxt_fast_path(p == block->start)) {
            nxt_rbtree_delete(&mp->blocks, &block->node);

            if (block->type == NXT_MP_DISCRETE_BLOCK) {
                mp->proto->free(mp->mem, block);
            }

            mp->proto->free(mp->mem, p);

            return;

        } else {
            err = "freed pointer points to middle of block: %p";
        }

    } else {
        err = "freed pointer is out of mp: %p";
    }

    if (mp->proto->alert != NULL) {
        mp->proto->alert(mp->trace, err, p);
    }
}


static nxt_mp_block_t *
nxt_mp_find_block(nxt_rbtree_t *tree, u_char *p)
{
    nxt_mp_block_t     *block;
    nxt_rbtree_node_t  *node, *sentinel;

    node = nxt_rbtree_root(tree);
    sentinel = nxt_rbtree_sentinel(tree);

    while (node != sentinel) {

        block = (nxt_mp_block_t *) node;

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
nxt_mp_chunk_free(nxt_mp_t *mp, nxt_mp_block_t *cluster,
    u_char *p)
{
    u_char         *start;
    uintptr_t      offset;
    nxt_uint_t     n, size, chunk;
    nxt_mp_page_t  *page;
    nxt_mp_slot_t  *slot;

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

        if (nxt_slow_path(offset != chunk * size)) {
            return "freed pointer points to wrong chunk: %p";
        }

        if (nxt_slow_path(nxt_mp_chunk_is_free(page->map, chunk))) {
            return "freed pointer points to already free chunk: %p";
        }

        nxt_mp_chunk_set_free(page->map, chunk);

        /* Find a slot with appropriate chunk size. */
        for (slot = mp->slots; slot->size < size; slot++) { /* void */ }

        if (page->chunks != slot->chunks) {
            page->chunks++;

            if (page->chunks == 1) {
                /*
                 * Add the page to the head of mp chunk slot list
                 * of pages with free chunks.
                 */
                nxt_queue_insert_head(&slot->pages, &page->link);
            }

            nxt_mp_free_junk(p, size);

            return NULL;

        } else {
            /*
             * All chunks are free, remove the page from mp chunk slot
             * list of pages with free chunks.
             */
            nxt_queue_remove(&page->link);
        }

    } else if (nxt_slow_path(p != start)) {
        return "invalid pointer to chunk: %p";
    }

    /* Add the free page to the mp's free pages tree. */

    page->size = 0;
    nxt_queue_insert_head(&mp->free_pages, &page->link);

    nxt_mp_free_junk(p, size);

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
         nxt_queue_remove(&page->link);
         page++;
         n--;
    } while (n != 0);

    nxt_rbtree_delete(&mp->blocks, &cluster->node);

    p = cluster->start;

    mp->proto->free(mp->mem, cluster);
    mp->proto->free(mp->mem, p);

    return NULL;
}
