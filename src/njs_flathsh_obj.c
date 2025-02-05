
/*
 * Copyright (C) NGINX, Inc.
 */

#include <njs_main.h>

/*
 * This code derived from flat hash for case when key_hash has unique value,
 * so no need for separate test if elements are equal.
 */

#define NJS_FLATHSH_OBJ_ELTS_INITIAL_SIZE         2
#define NJS_FLATHSH_OBJ_HASH_INITIAL_SIZE         4
#define NJS_FLATHSH_OBJ_ELTS_EXPAND_FACTOR_NUM    3
#define NJS_FLATHSH_OBJ_ELTS_EXPAND_FACTOR_DENOM  2
#define NJS_FLATHSH_OBJ_ELTS_FRACTION_TO_SHRINK   2
#define NJS_FLATHSH_OBJ_ELTS_MINIMUM_TO_SHRINK    8


static njs_flathsh_obj_descr_t *njs_flathsh_obj_alloc(
    njs_flathsh_obj_query_t *fhq, size_t hash_size, size_t elts_size);
njs_flathsh_obj_descr_t *njs_flathsh_obj_expand_elts(
    njs_flathsh_obj_query_t *fhq, njs_flathsh_obj_descr_t *h);


njs_inline size_t
njs_flathsh_obj_chunk_size(size_t hash_size, size_t elts_size)
{
    return hash_size * sizeof(uint32_t) + sizeof(njs_flathsh_obj_descr_t) +
           elts_size * sizeof(njs_flathsh_obj_elt_t);
}


njs_inline void *
njs_flathsh_obj_malloc(njs_flathsh_obj_query_t *fhq, size_t size)
{
    return
#ifdef NJS_FLATHSH_OBJ_USE_SYSTEM_ALLOCATOR
    malloc(size)
#else
    fhq->proto->alloc(fhq->pool, size)
#endif
    ;
}


njs_inline void
njs_flathsh_obj_free(njs_flathsh_obj_query_t *fhq, void *ptr)
{
#ifdef NJS_FLATHSH_OBJ_USE_SYSTEM_ALLOCATOR
    free(ptr)
#else
    fhq->proto->free(fhq->pool, ptr, 0)
#endif
    ;
}


njs_inline njs_flathsh_obj_descr_t *
njs_flathsh_obj_descr(void *chunk, size_t hash_size)
{
    return (njs_flathsh_obj_descr_t *) ((uint32_t *) chunk + hash_size);
}


njs_inline void *
njs_flathsh_obj_chunk(njs_flathsh_obj_descr_t *h)
{
    return njs_flathsh_obj_hash_cells_end(h) - ((njs_int_t) h->hash_mask + 1);
}


/*
 * Create a new empty flat hash.
 */
njs_flathsh_obj_descr_t *
njs_flathsh_obj_new(njs_flathsh_obj_query_t *fhq)
{
    return njs_flathsh_obj_alloc(fhq, NJS_FLATHSH_OBJ_HASH_INITIAL_SIZE,
                           NJS_FLATHSH_OBJ_ELTS_INITIAL_SIZE);
}


void
njs_flathsh_obj_destroy(njs_flathsh_obj_t *fh, njs_flathsh_obj_query_t *fhq)
{
    njs_flathsh_obj_free(fhq, njs_flathsh_obj_chunk(fh->slot));

    fh->slot = NULL;
}


static njs_flathsh_obj_descr_t *
njs_flathsh_obj_alloc(njs_flathsh_obj_query_t *fhq, size_t hash_size, size_t elts_size)
{
    void                     *chunk;
    size_t                   size;
    njs_flathsh_obj_descr_t  *h;

    njs_assert_msg(hash_size != 0 && (hash_size & (hash_size - 1)) == 0,
                   "obj hash_size must be a power of two");

    size = njs_flathsh_obj_chunk_size(hash_size, elts_size);

    chunk = njs_flathsh_obj_malloc(fhq, size);
    if (njs_slow_path(chunk == NULL)) {
        return NULL;
    }

    h = njs_flathsh_obj_descr(chunk, hash_size);

    njs_memzero(chunk, sizeof(uint32_t) * hash_size);

    h->hash_mask = hash_size - 1;
    h->elts_size = elts_size;
    h->elts_count = 0;
    h->elts_deleted_count = 0;

    return h;
}


njs_flathsh_obj_elt_t *
njs_flathsh_obj_add_elt(njs_flathsh_obj_t *fh, njs_flathsh_obj_query_t *fhq)
{
    njs_int_t                cell_num;
    njs_flathsh_obj_elt_t    *elt, *elts;
    njs_flathsh_obj_descr_t  *h;

    h = fh->slot;
    if (njs_slow_path(h == NULL)) {
        return NULL;
    }

    if (njs_slow_path(h->elts_count == h->elts_size)) {
        h = njs_flathsh_obj_expand_elts(fhq, h);
        if (njs_slow_path(h == NULL)) {
            return NULL;
        }

        fh->slot = h;
    }

    elts = njs_flathsh_obj_hash_elts(h);
    elt = &elts[h->elts_count++];

    elt->value = fhq->value;
    elt->key_hash = fhq->key_hash;

    cell_num = fhq->key_hash & h->hash_mask;
    elt->next_elt = njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1];
    njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1] = h->elts_count;

    return elt;
}


njs_flathsh_obj_descr_t *
njs_flathsh_obj_expand_elts(njs_flathsh_obj_query_t *fhq, njs_flathsh_obj_descr_t *h)
{
    void                     *chunk;
    size_t                   size, new_elts_size, new_hash_size;
    uint32_t                 new_hash_mask, i;
    njs_int_t                cell_num;
    njs_flathsh_obj_elt_t    *elt;
    njs_flathsh_obj_descr_t  *h_src;

    new_elts_size = h->elts_size * (size_t) NJS_FLATHSH_OBJ_ELTS_EXPAND_FACTOR_NUM /
                                          NJS_FLATHSH_OBJ_ELTS_EXPAND_FACTOR_DENOM;

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
        size = njs_flathsh_obj_chunk_size(new_hash_size, new_elts_size);
        chunk = njs_flathsh_obj_malloc(fhq, size);
        if (njs_slow_path(chunk == NULL)) {
            return NULL;
        }

        h = njs_flathsh_obj_descr(chunk, new_hash_size);

        memcpy(h, h_src, sizeof(njs_flathsh_obj_descr_t) +
               sizeof(njs_flathsh_obj_elt_t) * h_src->elts_size);

        new_hash_mask = new_hash_size - 1;
        h->hash_mask = new_hash_mask;
        njs_memzero(chunk, sizeof(uint32_t) * new_hash_size);

        for (i = 0, elt = njs_flathsh_obj_hash_elts(h); i < h->elts_count; i++, elt++) {
            if (elt->value != NULL) {
                cell_num = elt->key_hash & new_hash_mask;
                elt->next_elt = njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1];
                njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1] = i + 1;
            }
        }

        njs_flathsh_obj_free(fhq, njs_flathsh_obj_chunk(h_src));

    } else {

        size = njs_flathsh_obj_chunk_size(new_hash_size, new_elts_size);

        /* Expand elts only. */
#ifdef NJS_FLATHSH_OBJ_USE_SYSTEM_ALLOCATOR
        chunk = realloc(njs_flathsh_obj_chunk(h), size);
        if (njs_slow_path(chunk == NULL)) {
            return NULL;
        }

#else
        chunk = fhq->proto->alloc(fhq->pool, size);
        if (njs_slow_path(chunk == NULL)) {
            return NULL;
        }

        memcpy(chunk, njs_flathsh_obj_chunk(h),
               njs_flathsh_obj_chunk_size(h->hash_mask + 1, h->elts_size));

        fhq->proto->free(fhq->pool, njs_flathsh_obj_chunk(h), 0);
#endif
        h = njs_flathsh_obj_descr(chunk, new_hash_size);
    }

    h->elts_size = new_elts_size;

    return h;
}


njs_int_t
njs_flathsh_obj_find(const njs_flathsh_obj_t *fh, njs_flathsh_obj_query_t *fhq)
{
    njs_int_t                cell_num, elt_num;
    njs_flathsh_obj_elt_t    *e, *elts;
    njs_flathsh_obj_descr_t  *h;

    h = fh->slot;
    if (njs_slow_path(h == NULL)) {
        return NJS_DECLINED;
    }

    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1];
    elts = njs_flathsh_obj_hash_elts(h);

    while (elt_num != 0) {
        e = &elts[elt_num - 1];

        /* TODO: need to be replaced by atomic test. */

        if (e->key_hash == fhq->key_hash)
        {
            fhq->value = e->value;
            return NJS_OK;
        }

        elt_num = e->next_elt;
    }

    return NJS_DECLINED;
}


njs_int_t
njs_flathsh_obj_insert(njs_flathsh_obj_t *fh, njs_flathsh_obj_query_t *fhq)
{
    void                     *tmp;
    njs_int_t                cell_num, elt_num;
    njs_flathsh_obj_elt_t    *elt, *elts;
    njs_flathsh_obj_descr_t  *h;

    h = fh->slot;

    if (h == NULL) {
        h = njs_flathsh_obj_new(fhq);
        if (h == NULL) {
            return NJS_ERROR;
        }

        fh->slot = h;
    }

    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1];
    elts = njs_flathsh_obj_hash_elts(h);

    while (elt_num != 0) {
        elt = &elts[elt_num - 1];

        /* TODO: need to be replaced by atomic test. */

        if (elt->key_hash == fhq->key_hash)
        {
            if (fhq->replace) {
                tmp = fhq->value;
                fhq->value = elt->value;
                elt->value = tmp;

                return NJS_OK;

            } else {
                fhq->value = elt->value;

                return NJS_DECLINED;
            }
        }

        elt_num = elt->next_elt;
    }

    elt = njs_flathsh_obj_add_elt(fh, fhq);
    if (elt == NULL) {
        return NJS_ERROR;
    }

    elt->value = fhq->value;

    return NJS_OK;
}


static njs_flathsh_obj_descr_t *
njs_flathsh_obj_shrink_elts(njs_flathsh_obj_query_t *fhq, njs_flathsh_obj_descr_t *h)
{
    void                     *chunk;
    njs_int_t                cell_num;
    uint32_t                 i, j, new_hash_size, new_hash_mask, new_elts_size;
    njs_flathsh_obj_elt_t    *elt, *elt_src;
    njs_flathsh_obj_descr_t  *h_src;

    new_elts_size = njs_max(NJS_FLATHSH_OBJ_ELTS_INITIAL_SIZE,
                            h->elts_count - h->elts_deleted_count);

    njs_assert(new_elts_size <= h->elts_size);

    new_hash_size = h->hash_mask + 1;
    while ((new_hash_size / 2) >= new_elts_size) {
        new_hash_size = new_hash_size / 2;
    }

    new_hash_mask = new_hash_size - 1;

    h_src = h;
    chunk = njs_flathsh_obj_malloc(fhq, njs_flathsh_obj_chunk_size(new_hash_size,
                                                                 new_elts_size));
    if (njs_slow_path(chunk == NULL)) {
        return NULL;
    }

    h = njs_flathsh_obj_descr(chunk, new_hash_size);
    memcpy(h, h_src, sizeof(njs_flathsh_obj_descr_t));

    njs_memzero(njs_flathsh_obj_hash_cells_end(h) - new_hash_size,
                sizeof(uint32_t) * new_hash_size);

    elt_src = njs_flathsh_obj_hash_elts(h_src);
    for (i = 0, j = 0, elt = njs_flathsh_obj_hash_elts(h); i < h->elts_count; i++) {
        if (elt_src->value != NULL) {
            elt->value = elt_src->value;
            elt->key_hash = elt_src->key_hash;

            cell_num = elt_src->key_hash & new_hash_mask;
            elt->next_elt = njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1];
            njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1] = j + 1;
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

    njs_flathsh_obj_free(fhq, njs_flathsh_obj_chunk(h_src));

    return h;
}


njs_int_t
njs_flathsh_obj_delete(njs_flathsh_obj_t *fh, njs_flathsh_obj_query_t *fhq)
{
    njs_int_t                cell_num, elt_num;
    njs_flathsh_obj_elt_t    *elt, *elt_prev, *elts;
    njs_flathsh_obj_descr_t  *h;

    h = fh->slot;

    if (njs_slow_path(h == NULL)) {
        return NJS_DECLINED;
    }

    cell_num = fhq->key_hash & h->hash_mask;
    elt_num = njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1];
    elts = njs_flathsh_obj_hash_elts(h);
    elt_prev = NULL;

    while (elt_num != 0) {
        elt = &elts[elt_num - 1];

        /* TODO: use atomic comparision. */

        if (elt->key_hash == fhq->key_hash)
        {
            fhq->value = elt->value;

            if (elt_prev != NULL) {
                elt_prev->next_elt = elt->next_elt;

            } else {
                njs_flathsh_obj_hash_cells_end(h)[-cell_num - 1] = elt->next_elt;
            }

            h->elts_deleted_count++;

            elt->value = NULL;

            /* Shrink elts if elts_deleted_count is eligible. */

            if (h->elts_deleted_count >= NJS_FLATHSH_OBJ_ELTS_MINIMUM_TO_SHRINK
                && h->elts_deleted_count
                   >= (h->elts_count / NJS_FLATHSH_OBJ_ELTS_FRACTION_TO_SHRINK))
            {
                h = njs_flathsh_obj_shrink_elts(fhq, h);
                if (njs_slow_path(h == NULL)) {
                    return NJS_ERROR;
                }

                fh->slot = h;
            }

            if (h->elts_deleted_count == h->elts_count) {
                njs_flathsh_obj_free(fhq, njs_flathsh_obj_chunk(h));
                fh->slot = NULL;
            }

            return NJS_OK;
        }

        elt_prev = elt;
        elt_num = elt->next_elt;
    }

    return NJS_DECLINED;
}


njs_flathsh_obj_elt_t *
njs_flathsh_obj_each(const njs_flathsh_obj_t *fh, njs_flathsh_obj_each_t *fhe)
{
    njs_flathsh_obj_elt_t    *e, *elt;
    njs_flathsh_obj_descr_t  *h;

    h = fh->slot;
    if (njs_slow_path(h == NULL)) {
        return NULL;
    }

    elt = njs_flathsh_obj_hash_elts(h);

    while (fhe->cp < h->elts_count) {
        e = &elt[fhe->cp++];
        if (e->value != NULL) {
            return e;
        }
    }

    return NULL;
}


njs_int_t
njs_flathsh_obj_alloc_copy(njs_mp_t *mp, njs_flathsh_obj_t *to, njs_flathsh_obj_t *from)
{
    void                    *from_chunk, *to_chunk;
    uint32_t                from_size;
    njs_flathsh_obj_descr_t *from_descr;

    from_descr = from->slot;

    from_size = sizeof(uint32_t) * (from_descr->hash_mask + 1ul) +
                sizeof(njs_flathsh_obj_descr_t) +
                sizeof(njs_flathsh_obj_elt_t) * from_descr->elts_size;

    to_chunk = njs_mp_alloc(mp, from_size);
    if (njs_slow_path(to_chunk == NULL)) {
        return NJS_ERROR;
    }

    from_chunk = njs_flathsh_obj_chunk(from_descr);

    memcpy(to_chunk, from_chunk, from_size);

    to->slot = (char *)to_chunk + ((char *)from_descr - (char *)from_chunk);

    return NJS_OK;
}
