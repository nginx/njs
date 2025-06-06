
/*
 * Copyright (C) NGINX, Inc.
 */

#include <njs_main.h>

/*
 * Flat hash consists of dynamic DATA STRUCTURE and set of OPERATIONS.
 *
 * DATA STRUCTURE
 *    Consists of 3 parts allocated one by one in chunk of memory:
 *
 *    HASH_CELLS   array of indices of 1st list element in ELEMENTS array,
 *                 or 0 if list is empty. HASH_CELLS_length is power of 2.
 *    DESCRIPTOR   contains hash_mask (= HASH_CELLS_Length - 1), ELEMENTS_size,
 *                 number of last used element in ELEMENTS,
 *                 number of deleted elements in ELEMENTS;
 *    ELEMENTS     array of: [index of next element, hash_function(KEY),
 *                 link to stored value or NULL if element is not used)].
 *
 * PROPERTIES of flat hash
 *     It is relocatable in memory, preserve insertion order, expand and shrink
 *     depending on number elements in it, maximum occupancy is 2^32-2 elements.
 *
 * OPERATIONS
 *    ADD element by key S with value V
 *      Prerequisite: be sure if flat hash not contains S. If ELEMENTS has free
 *      space after last element, then this element is populated by: V,
 *      hash_function(S), S. Then element is added to correspondent HASH_CELL.
 *      In case when no free element in ELEMENTS, DATA STRUCTURE is expanded by
 *      expnad_elts(). It does the following: ELEMENTS_size is increased by
 *      EXPAND_FACTOR, which value is expected to be > 1. For fast access to
 *      stored values, HASH_CELLS_size need to be big enough to provide its low
 *      population: in average less than 1 element per HASH_CELL.  So,
 *      if HASH_CELLS_size < ELEMENTS_size then it will try doubling
 *      HASH_CELLS_size, until new HASH_CELLS_size >= ELEMENTS_size. Now
 *      new HASH_CELLS_size and new ELEMENTS_size are both defined. New
 *      expanded hash is obtained as:
 *          if HASH_CELLS_size is not increased, then
 *              reallocate full DATA STRUCTURE,
 *          else
 *              create new DATA STRUCTURE and populate it
 *              by ELEMENTS from old DATA STRUCTURE.
 *          Replace old DATA STRUCTURE by new one and release old one.
 *
 *    FIND element by key S
 *      HASH_CELLS is array which contains cells of hash
 *      table. As entry to the table the following index is used:
 *          cell_num = hash_function(S) & hash_nask
 *      hash_function is external and it is not specified here, it is needed to
 *      be good hash function for Ss, and produce results in range from 0 to
 *      at least 2^32 - 1; hash_mask is located in DESCRIPTOR, and it is equal
 *      to HASH_CELLS_size - 1, where HASH_CELLS_size is always power of 2.
 *      hash cell contains (may be empty) list of hash elements with same
 *      cell_num. Now run over the list of elements and test if some element
 *      contains link to S. Test function is external and is not specified here.
 *
 *    DELETE element by key S
 *      Locate S in ELEMENTS and remove it from elements list. Update number
 *      of removed elements in hash_decriptor. Mark deleted
 *      element as not used/deleted. If number of deleted elements is big
 *      enough, then use shrink_elts(): it removes gaps in ELEMENTS, shrinks if
 *      required HASH_CELLS, and creates new DATA STRUCTURE.
 *
 *    ENUMERATE all elements in order of insertion
 *      Returns one by one used elements from ELEMENTS.
 */


#define NJS_FLATHSH_ELTS_INITIAL_SIZE         2
#define NJS_FLATHSH_HASH_INITIAL_SIZE         4
#define NJS_FLATHSH_ELTS_EXPAND_FACTOR_NUM    3
#define NJS_FLATHSH_ELTS_EXPAND_FACTOR_DENOM  2
#define NJS_FLATHSH_ELTS_FRACTION_TO_SHRINK   2
#define NJS_FLATHSH_ELTS_MINIMUM_TO_SHRINK    8


static njs_flathsh_descr_t *njs_flathsh_alloc(njs_flathsh_query_t *fhq,
    size_t hash_size, size_t elts_size);
static njs_flathsh_descr_t *njs_expand_elts(njs_flathsh_query_t *fhq,
    njs_flathsh_descr_t *h);


njs_inline size_t
njs_flathsh_chunk_size(size_t hash_size, size_t elts_size)
{
    return hash_size * sizeof(uint32_t) + sizeof(njs_flathsh_descr_t) +
           elts_size * sizeof(njs_flathsh_elt_t);
}


njs_inline void *
njs_flathsh_malloc(njs_flathsh_query_t *fhq, size_t size)
{
    return
#ifdef NJS_FLATHSH_USE_SYSTEM_ALLOCATOR
    malloc(size)
#else
    fhq->proto->alloc(fhq->pool, size)
#endif
    ;
}


njs_inline void
njs_flathsh_free(njs_flathsh_query_t *fhq, void *ptr)
{
#ifdef NJS_FLATHSH_USE_SYSTEM_ALLOCATOR
    free(ptr)
#else
    fhq->proto->free(fhq->pool, ptr, 0)
#endif
    ;
}


njs_inline njs_flathsh_descr_t *
njs_flathsh_descr(void *chunk, size_t hash_size)
{
    return (njs_flathsh_descr_t *) ((uint32_t *) chunk + hash_size);
}


njs_inline uint32_t *
njs_hash_cells_end(njs_flathsh_descr_t *h)
{
    return (uint32_t *) h;
}


njs_inline void *
njs_flathsh_chunk(njs_flathsh_descr_t *h)
{
    return njs_hash_cells_end(h) - ((njs_int_t) h->hash_mask + 1);
}


/*
 * Create a new empty flat hash.
 */
njs_flathsh_descr_t *
njs_flathsh_new(njs_flathsh_query_t *fhq)
{
    return njs_flathsh_alloc(fhq, NJS_FLATHSH_HASH_INITIAL_SIZE,
                           NJS_FLATHSH_ELTS_INITIAL_SIZE);
}


void
njs_flathsh_destroy(njs_flathsh_t *fh, njs_flathsh_query_t *fhq)
{
    njs_flathsh_free(fhq, njs_flathsh_chunk(fh->slot));

    fh->slot = NULL;
}


static njs_flathsh_descr_t *
njs_flathsh_alloc(njs_flathsh_query_t *fhq, size_t hash_size, size_t elts_size)
{
    void                 *chunk;
    size_t               size;
    njs_flathsh_descr_t  *h;

    njs_assert_msg(hash_size != 0 && (hash_size & (hash_size - 1)) == 0,
                   "hash_size must be a power of two");

    size = njs_flathsh_chunk_size(hash_size, elts_size);

    chunk = njs_flathsh_malloc(fhq, size);
    if (njs_slow_path(chunk == NULL)) {
        return NULL;
    }

    h = njs_flathsh_descr(chunk, hash_size);

    njs_memzero(chunk, sizeof(uint32_t) * hash_size);

    h->hash_mask = hash_size - 1;
    h->elts_size = elts_size;
    h->elts_count = 0;
    h->elts_deleted_count = 0;

    return h;
}


njs_flathsh_elt_t *
njs_flathsh_add_elt(njs_flathsh_t *fh, njs_flathsh_query_t *fhq)
{
    njs_int_t            cell_num;
    njs_flathsh_elt_t    *elt, *elts;
    njs_flathsh_descr_t  *h;

    h = fh->slot;
    if (njs_slow_path(h == NULL)) {
        return NULL;
    }

    if (njs_slow_path(h->elts_count == h->elts_size)) {
        h = njs_expand_elts(fhq, h);
        if (njs_slow_path(h == NULL)) {
            return NULL;
        }

        fh->slot = h;
    }

    elts = njs_hash_elts(h);
    elt = &elts[h->elts_count++];

    elt->key_hash = fhq->key_hash;

    cell_num = fhq->key_hash & h->hash_mask;
    elt->next_elt = njs_hash_cells_end(h)[-cell_num - 1];
    njs_hash_cells_end(h)[-cell_num - 1] = h->elts_count;

    elt->type = NJS_PROPERTY;

    return elt;
}


static njs_flathsh_descr_t *
njs_expand_elts(njs_flathsh_query_t *fhq, njs_flathsh_descr_t *h)
{
    void                 *chunk;
    size_t               size, new_elts_size, new_hash_size;
    uint32_t             new_hash_mask, i;
    njs_int_t            cell_num;
    njs_flathsh_elt_t    *elt;
    njs_flathsh_descr_t  *h_src;

    new_elts_size = h->elts_size * (size_t) NJS_FLATHSH_ELTS_EXPAND_FACTOR_NUM /
                                          NJS_FLATHSH_ELTS_EXPAND_FACTOR_DENOM;

    new_elts_size = njs_max(h->elts_count + 1ul, new_elts_size);

    new_hash_size = h->hash_mask + 1ul;

    while (new_hash_size < new_elts_size) {
        new_hash_size = 2 * new_hash_size;
    }

    /* Overflow check. */

    if (njs_slow_path(new_hash_size > UINT32_MAX)) {
        return NULL;
    }

    if (new_hash_size != (h->hash_mask + 1)) {

        /* Expand both hash table cells and its elts. */

        h_src = h;
        size = njs_flathsh_chunk_size(new_hash_size, new_elts_size);
        chunk = njs_flathsh_malloc(fhq, size);
        if (njs_slow_path(chunk == NULL)) {
            return NULL;
        }

        h = njs_flathsh_descr(chunk, new_hash_size);

        memcpy(h, h_src, sizeof(njs_flathsh_descr_t) +
               sizeof(njs_flathsh_elt_t) * h_src->elts_size);

        new_hash_mask = new_hash_size - 1;
        h->hash_mask = new_hash_mask;
        njs_memzero(chunk, sizeof(uint32_t) * new_hash_size);

        for (i = 0, elt = njs_hash_elts(h); i < h->elts_count; i++, elt++) {
            if (elt->type != NJS_FREE_FLATHSH_ELEMENT) {
                cell_num = elt->key_hash & new_hash_mask;
                elt->next_elt = njs_hash_cells_end(h)[-cell_num - 1];
                njs_hash_cells_end(h)[-cell_num - 1] = i + 1;
            }
        }

        njs_flathsh_free(fhq, njs_flathsh_chunk(h_src));

    } else {

        size = njs_flathsh_chunk_size(new_hash_size, new_elts_size);

        /* Expand elts only. */
#ifdef NJS_FLATHSH_USE_SYSTEM_ALLOCATOR
        chunk = realloc(njs_flathsh_chunk(h), size);
        if (njs_slow_path(chunk == NULL)) {
            return NULL;
        }

#else
        chunk = fhq->proto->alloc(fhq->pool, size);
        if (njs_slow_path(chunk == NULL)) {
            return NULL;
        }

        memcpy(chunk, njs_flathsh_chunk(h),
               njs_flathsh_chunk_size(h->hash_mask + 1, h->elts_size));

        fhq->proto->free(fhq->pool, njs_flathsh_chunk(h), 0);
#endif
        h = njs_flathsh_descr(chunk, new_hash_size);
    }

    h->elts_size = new_elts_size;

    return h;
}


njs_int_t
njs_flathsh_find(const njs_flathsh_t *fh, njs_flathsh_query_t *fhq)
{
    njs_int_t            cell_num, elt_num;
    njs_flathsh_elt_t    *e, *elts;
    njs_flathsh_descr_t  *h;

    h = fh->slot;
    if (njs_slow_path(h == NULL)) {
        return NJS_DECLINED;
    }

    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_hash_cells_end(h)[-cell_num - 1];
    elts = njs_hash_elts(h);

    while (elt_num != 0) {
        e = &elts[elt_num - 1];

        if (e->key_hash == fhq->key_hash &&
            fhq->proto->test(fhq, e->value) == NJS_OK)
        {
            fhq->value = e;
            return NJS_OK;
        }

        elt_num = e->next_elt;
    }

    return NJS_DECLINED;
}


njs_int_t
njs_flathsh_unique_find(const njs_flathsh_t *fh, njs_flathsh_query_t *fhq)
{
    njs_int_t            cell_num, elt_num;
    njs_flathsh_elt_t    *e, *elts;
    njs_flathsh_descr_t  *h;

    h = fh->slot;
    if (njs_slow_path(h == NULL)) {
        return NJS_DECLINED;
    }

    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_hash_cells_end(h)[-cell_num - 1];
    elts = njs_hash_elts(h);

    while (elt_num != 0) {
        e = &elts[elt_num - 1];

        if (e->key_hash == fhq->key_hash) {
            fhq->value = e;
            return NJS_OK;
        }

        elt_num = e->next_elt;
    }

    return NJS_DECLINED;
}


njs_int_t
njs_flathsh_insert(njs_flathsh_t *fh, njs_flathsh_query_t *fhq)
{
    njs_int_t            cell_num, elt_num;
    njs_flathsh_elt_t    *elt, *elts;
    njs_flathsh_descr_t  *h;

    h = fh->slot;

    if (h == NULL) {
        h = njs_flathsh_new(fhq);
        if (h == NULL) {
            return NJS_ERROR;
        }

        fh->slot = h;
    }

    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_hash_cells_end(h)[-cell_num - 1];
    elts = njs_hash_elts(h);

    while (elt_num != 0) {
        elt = &elts[elt_num - 1];

        if (elt->key_hash == fhq->key_hash &&
            fhq->proto->test(fhq, elt->value) == NJS_OK)
        {
            if (fhq->replace) {
                fhq->value = elt;
                return NJS_OK;

            } else {
                return NJS_DECLINED;
            }
        }

        elt_num = elt->next_elt;
    }

    elt = njs_flathsh_add_elt(fh, fhq);
    if (elt == NULL) {
        return NJS_ERROR;
    }

    fhq->value = elt;

    return NJS_OK;
}


njs_int_t
njs_flathsh_unique_insert(njs_flathsh_t *fh, njs_flathsh_query_t *fhq)
{
    njs_int_t            cell_num, elt_num;
    njs_flathsh_elt_t    *elt, *elts;
    njs_flathsh_descr_t  *h;

    h = fh->slot;

    if (h == NULL) {
        h = njs_flathsh_new(fhq);
        if (h == NULL) {
            return NJS_ERROR;
        }

        fh->slot = h;
    }

    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_hash_cells_end(h)[-cell_num - 1];
    elts = njs_hash_elts(h);

    while (elt_num != 0) {
        elt = &elts[elt_num - 1];

        if (elt->key_hash == fhq->key_hash) {
            if (fhq->replace) {
                fhq->value = elt;
                return NJS_OK;

            } else {
                return NJS_DECLINED;
            }
        }

        elt_num = elt->next_elt;
    }

    elt = njs_flathsh_add_elt(fh, fhq);
    if (elt == NULL) {
        return NJS_ERROR;
    }

    fhq->value = elt;

    return NJS_OK;
}


static njs_flathsh_descr_t *
njs_shrink_elts(njs_flathsh_query_t *fhq, njs_flathsh_descr_t *h)
{
    void                 *chunk;
    njs_int_t            cell_num;
    uint32_t             i, j, new_hash_size, new_hash_mask, new_elts_size;
    njs_flathsh_elt_t    *elt, *elt_src;
    njs_flathsh_descr_t  *h_src;

    new_elts_size = njs_max(NJS_FLATHSH_ELTS_INITIAL_SIZE,
                            h->elts_count - h->elts_deleted_count);

    njs_assert(new_elts_size <= h->elts_size);

    new_hash_size = h->hash_mask + 1;
    while ((new_hash_size / 2) >= new_elts_size) {
        new_hash_size = new_hash_size / 2;
    }

    new_hash_mask = new_hash_size - 1;

    h_src = h;
    chunk = njs_flathsh_malloc(fhq, njs_flathsh_chunk_size(new_hash_size,
                                                           new_elts_size));
    if (njs_slow_path(chunk == NULL)) {
        return NULL;
    }

    h = njs_flathsh_descr(chunk, new_hash_size);
    memcpy(h, h_src, sizeof(njs_flathsh_descr_t));

    njs_memzero(njs_hash_cells_end(h) - new_hash_size,
                sizeof(uint32_t) * new_hash_size);

    elt_src = njs_hash_elts(h_src);
    for (i = 0, j = 0, elt = njs_hash_elts(h); i < h->elts_count; i++) {
        if (elt_src->type != NJS_FREE_FLATHSH_ELEMENT) {
            *elt = *elt_src;
            elt->key_hash = elt_src->key_hash;

            cell_num = elt_src->key_hash & new_hash_mask;
            elt->next_elt = njs_hash_cells_end(h)[-cell_num - 1];
            njs_hash_cells_end(h)[-cell_num - 1] = j + 1;
            j++;
            elt++;
        }

        elt_src++;
    }

    njs_assert(j == (h->elts_count - h->elts_deleted_count));

    h->hash_mask = new_hash_mask;
    h->elts_size = new_elts_size;
    h->elts_deleted_count = 0;
    h->elts_count = j;

    njs_flathsh_free(fhq, njs_flathsh_chunk(h_src));

    return h;
}


njs_int_t
njs_flathsh_delete(njs_flathsh_t *fh, njs_flathsh_query_t *fhq)
{
    njs_int_t            cell_num, elt_num;
    njs_flathsh_elt_t    *elt, *elt_prev, *elts;
    njs_flathsh_descr_t  *h;

    h = fh->slot;

    if (njs_slow_path(h == NULL)) {
        return NJS_DECLINED;
    }

    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_hash_cells_end(h)[-cell_num - 1];
    elts = njs_hash_elts(h);
    elt_prev = NULL;

    while (elt_num != 0) {
        elt = &elts[elt_num - 1];

        if (elt->key_hash == fhq->key_hash &&
            fhq->proto->test(fhq, elt->value) == NJS_OK)
        {
            fhq->value = elt;

            if (elt_prev != NULL) {
                elt_prev->next_elt = elt->next_elt;

            } else {
                njs_hash_cells_end(h)[-cell_num - 1] = elt->next_elt;
            }

            h->elts_deleted_count++;

            elt->type = NJS_FREE_FLATHSH_ELEMENT;

            /* Shrink elts if elts_deleted_count is eligible. */

            if (h->elts_deleted_count >= NJS_FLATHSH_ELTS_MINIMUM_TO_SHRINK
                && h->elts_deleted_count
                   >= (h->elts_count / NJS_FLATHSH_ELTS_FRACTION_TO_SHRINK))
            {
                h = njs_shrink_elts(fhq, h);
                if (njs_slow_path(h == NULL)) {
                    return NJS_ERROR;
                }

                fh->slot = h;
            }

            if (h->elts_deleted_count == h->elts_count) {
                njs_flathsh_free(fhq, njs_flathsh_chunk(h));
                fh->slot = NULL;
            }

            return NJS_OK;
        }

        elt_prev = elt;
        elt_num = elt->next_elt;
    }

    return NJS_DECLINED;
}


njs_int_t
njs_flathsh_unique_delete(njs_flathsh_t *fh, njs_flathsh_query_t *fhq)
{
    njs_int_t            cell_num, elt_num;
    njs_flathsh_elt_t    *elt, *elt_prev, *elts;
    njs_flathsh_descr_t  *h;

    h = fh->slot;

    if (njs_slow_path(h == NULL)) {
        return NJS_DECLINED;
    }

    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_hash_cells_end(h)[-cell_num - 1];
    elts = njs_hash_elts(h);
    elt_prev = NULL;

    while (elt_num != 0) {
        elt = &elts[elt_num - 1];

        if (elt->key_hash == fhq->key_hash) {
            fhq->value = elt;

            if (elt_prev != NULL) {
                elt_prev->next_elt = elt->next_elt;

            } else {
                njs_hash_cells_end(h)[-cell_num - 1] = elt->next_elt;
            }

            h->elts_deleted_count++;

            elt->type = NJS_FREE_FLATHSH_ELEMENT;

            /* Shrink elts if elts_deleted_count is eligible. */

            if (h->elts_deleted_count >= NJS_FLATHSH_ELTS_MINIMUM_TO_SHRINK
                && h->elts_deleted_count
                   >= (h->elts_count / NJS_FLATHSH_ELTS_FRACTION_TO_SHRINK))
            {
                h = njs_shrink_elts(fhq, h);
                if (njs_slow_path(h == NULL)) {
                    return NJS_ERROR;
                }

                fh->slot = h;
            }

            if (h->elts_deleted_count == h->elts_count) {
                njs_flathsh_free(fhq, njs_flathsh_chunk(h));
                fh->slot = NULL;
            }

            return NJS_OK;
        }

        elt_prev = elt;
        elt_num = elt->next_elt;
    }

    return NJS_DECLINED;
}


njs_flathsh_elt_t *
njs_flathsh_each(const njs_flathsh_t *fh, njs_flathsh_each_t *fhe)
{
    njs_flathsh_elt_t    *e, *elt;
    njs_flathsh_descr_t  *h;

    h = fh->slot;
    if (njs_slow_path(h == NULL)) {
        return NULL;
    }

    elt = njs_hash_elts(h);

    while (fhe->cp < h->elts_count) {
        e = &elt[fhe->cp++];
        if (e->type != NJS_FREE_FLATHSH_ELEMENT) {
            return e;
        }
    }

    return NULL;
}
