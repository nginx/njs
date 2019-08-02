
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


/*
 * The level hash consists of hierarchical levels of arrays of pointers.
 * The pointers may point to another level, a bucket, or NULL.
 * The levels and buckets must be allocated in manner alike posix_memalign()
 * to bookkeep additional information in pointer low bits.
 *
 * A level is an array of pointers.  Its size is a power of 2.  Levels
 * may be different sizes, but on the same level the sizes are the same.
 * Level sizes are specified by number of bits per level in lvlhsh->shift
 * array.  A hash may have up to 7 levels.  There are two predefined
 * shift arrays given by the first two shift array values:
 *
 * 1) [0, 0]:  [4, 4, 4, 4, 4, 4, 4] on a 64-bit platform or
 *             [5, 5, 5, 5, 5, 5, 0] on a 32-bit platform,
 *    so default size of levels is 128 bytes.
 *
 * 2) [0, 10]: [10, 4, 4, 4, 4, 4, 0] on a 64-bit platform or
 *             [10, 5, 5, 5, 5, 0, 0] on a 32-bit platform,
 *    so default size of levels is 128 bytes on all levels except
 *    the first level.  The first level is 8K or 4K on 64-bit or 32-bit
 *    platforms respectively.
 *
 * All buckets in a hash are the same size which is a power of 2.
 * A bucket contains several entries stored and tested sequentially.
 * The bucket size should be one or two CPU cache line size, a minimum
 * allowed size is 32 bytes.  A default 128-byte bucket contains 10 64-bit
 * entries or 15 32-bit entries.  Each entry consists of pointer to value
 * data and 32-bit key.  If an entry value pointer is NULL, the entry is free.
 * On a 64-bit platform entry value pointers are no aligned, therefore they
 * are accessed as two 32-bit integers.  The rest trailing space in a bucket
 * is used as pointer to next bucket and this pointer is always aligned.
 * Although the level hash allows to store a lot of values in a bucket chain,
 * this is non optimal way.  The large data set should be stored using
 * several levels.
 */

#define njs_lvlhsh_is_bucket(p)                                               \
    ((uintptr_t) (p) & 1)


#define njs_lvlhsh_count_inc(n)                                               \
    n = (void *) ((uintptr_t) (n) + 2)


#define njs_lvlhsh_count_dec(n)                                               \
    n = (void *) ((uintptr_t) (n) - 2)


#define njs_lvlhsh_level_size(proto, nlvl)                                    \
    ((uintptr_t) 1 << proto->shift[nlvl])


#define njs_lvlhsh_level(lvl, mask)                                           \
    (void **) ((uintptr_t) lvl & (~mask << 2))


#define njs_lvlhsh_level_entries(lvl, mask)                                   \
    ((uintptr_t) lvl & (mask << 1))


#define njs_lvlhsh_store_bucket(slot, bkt)                                    \
    slot = (void **) ((uintptr_t) bkt | 2 | 1)


#define njs_lvlhsh_bucket_size(proto)                                         \
    proto->bucket_size


#define njs_lvlhsh_bucket(proto, bkt)                                         \
    (uint32_t *) ((uintptr_t) bkt & ~(uintptr_t) proto->bucket_mask)


#define njs_lvlhsh_bucket_entries(proto, bkt)                                 \
    (((uintptr_t) bkt & (uintptr_t) proto->bucket_mask) >> 1)


#define njs_lvlhsh_bucket_end(proto, bkt)                                     \
    &bkt[proto->bucket_end]


#define njs_lvlhsh_free_entry(e)                                              \
    (!(njs_lvlhsh_valid_entry(e)))


#define njs_lvlhsh_next_bucket(proto, bkt)                                    \
    ((void **) &bkt[proto->bucket_end])

#if (NJS_64BIT)

#define njs_lvlhsh_valid_entry(e)                                             \
    (((e)[0] | (e)[1]) != 0)


#define njs_lvlhsh_entry_value(e)                                             \
    (void *) (((uintptr_t) (e)[1] << 32) + (e)[0])


#define njs_lvlhsh_set_entry_value(e, n)                                      \
    (e)[0] = (uint32_t)  (uintptr_t) n;                                       \
    (e)[1] = (uint32_t) ((uintptr_t) n >> 32)


#define njs_lvlhsh_entry_key(e)                                               \
    (e)[2]


#define njs_lvlhsh_set_entry_key(e, n)                                        \
    (e)[2] = n

#else

#define njs_lvlhsh_valid_entry(e)                                             \
    ((e)[0] != 0)


#define njs_lvlhsh_entry_value(e)                                             \
    (void *) (e)[0]


#define njs_lvlhsh_set_entry_value(e, n)                                      \
    (e)[0] = (uint32_t) n


#define njs_lvlhsh_entry_key(e)                                               \
    (e)[1]


#define njs_lvlhsh_set_entry_key(e, n)                                        \
    (e)[1] = n

#endif


#define NJS_LVLHSH_BUCKET_DONE  ((void *) -1)


static njs_int_t njs_lvlhsh_level_find(njs_lvlhsh_query_t *lhq, void **lvl,
    uint32_t key, njs_uint_t nlvl);
static njs_int_t njs_lvlhsh_bucket_find(njs_lvlhsh_query_t *lhq, void **bkt);
static njs_int_t njs_lvlhsh_new_bucket(njs_lvlhsh_query_t *lhq, void **slot);
static njs_int_t njs_lvlhsh_level_insert(njs_lvlhsh_query_t *lhq,
    void **slot, uint32_t key, njs_uint_t nlvl);
static njs_int_t njs_lvlhsh_bucket_insert(njs_lvlhsh_query_t *lhq,
    void **slot, uint32_t key, njs_int_t nlvl);
static njs_int_t njs_lvlhsh_convert_bucket_to_level(njs_lvlhsh_query_t *lhq,
    void **slot, njs_uint_t nlvl, uint32_t *bucket);
static njs_int_t njs_lvlhsh_level_convertion_insert(njs_lvlhsh_query_t *lhq,
    void **parent, uint32_t key, njs_uint_t nlvl);
static njs_int_t njs_lvlhsh_bucket_convertion_insert(njs_lvlhsh_query_t *lhq,
    void **slot, uint32_t key, njs_int_t nlvl);
static njs_int_t njs_lvlhsh_free_level(njs_lvlhsh_query_t *lhq, void **level,
    njs_uint_t size);
static njs_int_t njs_lvlhsh_level_delete(njs_lvlhsh_query_t *lhq, void **slot,
    uint32_t key, njs_uint_t nlvl);
static njs_int_t njs_lvlhsh_bucket_delete(njs_lvlhsh_query_t *lhq, void **bkt);
static void *njs_lvlhsh_level_each(njs_lvlhsh_each_t *lhe, void **level,
    njs_uint_t nlvl, njs_uint_t shift);
static void *njs_lvlhsh_bucket_each(njs_lvlhsh_each_t *lhe);


njs_int_t
njs_lvlhsh_find(const njs_lvlhsh_t *lh, njs_lvlhsh_query_t *lhq)
{
    void  *slot;

    slot = lh->slot;

    if (njs_fast_path(slot != NULL)) {

        if (njs_lvlhsh_is_bucket(slot)) {
            return njs_lvlhsh_bucket_find(lhq, slot);
        }

        return njs_lvlhsh_level_find(lhq, slot, lhq->key_hash, 0);
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_lvlhsh_level_find(njs_lvlhsh_query_t *lhq, void **lvl, uint32_t key,
    njs_uint_t nlvl)
{
    void        **slot;
    uintptr_t   mask;
    njs_uint_t  shift;

    shift = lhq->proto->shift[nlvl];
    mask = ((uintptr_t) 1 << shift) - 1;

    lvl = njs_lvlhsh_level(lvl, mask);
    slot = lvl[key & mask];

    if (slot != NULL) {

        if (njs_lvlhsh_is_bucket(slot)) {
            return njs_lvlhsh_bucket_find(lhq, slot);
        }

        return njs_lvlhsh_level_find(lhq, slot, key >> shift, nlvl + 1);
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_lvlhsh_bucket_find(njs_lvlhsh_query_t *lhq, void **bkt)
{
    void        *value;
    uint32_t    *bucket, *e;
    njs_uint_t  n;

    do {
        bucket = njs_lvlhsh_bucket(lhq->proto, bkt);
        n = njs_lvlhsh_bucket_entries(lhq->proto, bkt);
        e = bucket;

        do {
            if (njs_lvlhsh_valid_entry(e)) {
                n--;

                if (njs_lvlhsh_entry_key(e) == lhq->key_hash) {

                    value = njs_lvlhsh_entry_value(e);

                    if (lhq->proto->test(lhq, value) == NJS_OK) {
                        lhq->value = value;

                        return NJS_OK;
                    }
                }
            }

            e += NJS_LVLHSH_ENTRY_SIZE;

        } while (n != 0);

        bkt = *njs_lvlhsh_next_bucket(lhq->proto, bucket);

    } while (bkt != NULL);

    return NJS_DECLINED;
}


njs_int_t
njs_lvlhsh_insert(njs_lvlhsh_t *lh, njs_lvlhsh_query_t *lhq)
{
    uint32_t  key;

    if (njs_fast_path(lh->slot != NULL)) {

        key = lhq->key_hash;

        if (njs_lvlhsh_is_bucket(lh->slot)) {
            return njs_lvlhsh_bucket_insert(lhq, &lh->slot, key, -1);
        }

        return njs_lvlhsh_level_insert(lhq, &lh->slot, key, 0);
    }

    return njs_lvlhsh_new_bucket(lhq, &lh->slot);
}


static njs_int_t
njs_lvlhsh_new_bucket(njs_lvlhsh_query_t *lhq, void **slot)
{
    uint32_t  *bucket;

    bucket = lhq->proto->alloc(lhq->pool, njs_lvlhsh_bucket_size(lhq->proto));

    if (njs_fast_path(bucket != NULL)) {

        njs_lvlhsh_set_entry_value(bucket, lhq->value);
        njs_lvlhsh_set_entry_key(bucket, lhq->key_hash);

        *njs_lvlhsh_next_bucket(lhq->proto, bucket) = NULL;

        njs_lvlhsh_store_bucket(*slot, bucket);

        return NJS_OK;
    }

    return NJS_ERROR;
}


static njs_int_t
njs_lvlhsh_level_insert(njs_lvlhsh_query_t *lhq, void **parent, uint32_t key,
    njs_uint_t nlvl)
{
    void        **slot, **lvl;
    njs_int_t   ret;
    uintptr_t   mask;
    njs_uint_t  shift;

    shift = lhq->proto->shift[nlvl];
    mask = ((uintptr_t) 1 << shift) - 1;

    lvl = njs_lvlhsh_level(*parent, mask);
    slot = &lvl[key & mask];

    if (*slot != NULL) {
        key >>= shift;

        if (njs_lvlhsh_is_bucket(*slot)) {
            return njs_lvlhsh_bucket_insert(lhq, slot, key, nlvl);
        }

        return njs_lvlhsh_level_insert(lhq, slot, key, nlvl + 1);
    }

    ret = njs_lvlhsh_new_bucket(lhq, slot);

    if (njs_fast_path(ret == NJS_OK)) {
        njs_lvlhsh_count_inc(*parent);
    }

    return ret;
}


static njs_int_t
njs_lvlhsh_bucket_insert(njs_lvlhsh_query_t *lhq, void **slot, uint32_t key,
    njs_int_t nlvl)
{
    void                      **bkt, **vacant_bucket, *value;
    uint32_t                  *bucket, *e, *vacant_entry;
    njs_int_t                 ret;
    uintptr_t                 n;
    const void                *new_value;
    const njs_lvlhsh_proto_t  *proto;

    bkt = slot;
    vacant_entry = NULL;
    vacant_bucket = NULL;
    proto = lhq->proto;

    /* Search for duplicate entry in bucket chain. */

    do {
        bucket = njs_lvlhsh_bucket(proto, *bkt);
        n = njs_lvlhsh_bucket_entries(proto, *bkt);
        e = bucket;

        do {
            if (njs_lvlhsh_valid_entry(e)) {

                if (njs_lvlhsh_entry_key(e) == lhq->key_hash) {

                    value = njs_lvlhsh_entry_value(e);

                    if (proto->test(lhq, value) == NJS_OK) {

                        new_value = lhq->value;
                        lhq->value = value;

                        if (lhq->replace) {
                            njs_lvlhsh_set_entry_value(e, new_value);

                            return NJS_OK;
                        }

                        return NJS_DECLINED;
                    }
                }

                n--;

            } else {
                /*
                 * Save a hole vacant position in bucket
                 * and continue to search for duplicate entry.
                 */
                if (vacant_entry == NULL) {
                    vacant_entry = e;
                    vacant_bucket = bkt;
                }
            }

            e += NJS_LVLHSH_ENTRY_SIZE;

        } while (n != 0);

        if (e < njs_lvlhsh_bucket_end(proto, bucket)) {
            /*
             * Save a vacant position on incomplete bucket's end
             * and continue to search for duplicate entry.
             */
            if (vacant_entry == NULL) {
                vacant_entry = e;
                vacant_bucket = bkt;
            }
        }

        bkt = njs_lvlhsh_next_bucket(proto, bucket);

    } while (*bkt != NULL);

    if (vacant_entry != NULL) {
        njs_lvlhsh_set_entry_value(vacant_entry, lhq->value);
        njs_lvlhsh_set_entry_key(vacant_entry, lhq->key_hash);
        njs_lvlhsh_count_inc(*vacant_bucket);

        return NJS_OK;
    }

    /* All buckets are full. */

    nlvl++;

    if (njs_fast_path(proto->shift[nlvl] != 0)) {

        ret = njs_lvlhsh_convert_bucket_to_level(lhq, slot, nlvl, bucket);

        if (njs_fast_path(ret == NJS_OK)) {
            return njs_lvlhsh_level_insert(lhq, slot, key, nlvl);
        }

        return ret;
    }

    /* The last allowed level, only buckets may be allocated here. */

    return njs_lvlhsh_new_bucket(lhq, bkt);
}


static njs_int_t
njs_lvlhsh_convert_bucket_to_level(njs_lvlhsh_query_t *lhq, void **slot,
    njs_uint_t nlvl, uint32_t *bucket)
{
    void                      *lvl, **level;
    uint32_t                  *e, *end, key;
    njs_int_t                 ret;
    njs_uint_t                i, shift, size;
    njs_lvlhsh_query_t        q;
    const njs_lvlhsh_proto_t  *proto;

    proto = lhq->proto;
    size = njs_lvlhsh_level_size(proto, nlvl);

    lvl = proto->alloc(lhq->pool, size * (sizeof(void *)));

    if (njs_slow_path(lvl == NULL)) {
        return NJS_ERROR;
    }

    njs_memzero(lvl, size * (sizeof(void *)));

    level = lvl;
    shift = 0;

    for (i = 0; i < nlvl; i++) {
        /*
         * Using SIMD operations in this trivial loop with maximum
         * 8 iterations may increase code size by 170 bytes.
         */
        njs_pragma_loop_disable_vectorization;

        shift += proto->shift[i];
    }

    end = njs_lvlhsh_bucket_end(proto, bucket);

    for (e = bucket; e < end; e += NJS_LVLHSH_ENTRY_SIZE) {

        q.proto = proto;
        q.pool = lhq->pool;
        q.value = njs_lvlhsh_entry_value(e);
        key = njs_lvlhsh_entry_key(e);
        q.key_hash = key;

        ret = njs_lvlhsh_level_convertion_insert(&q, &lvl, key >> shift, nlvl);

        if (njs_slow_path(ret != NJS_OK)) {
            return njs_lvlhsh_free_level(lhq, level, size);
        }
    }

    *slot = lvl;

    proto->free(lhq->pool, bucket, njs_lvlhsh_bucket_size(proto));

    return NJS_OK;
}


static njs_int_t
njs_lvlhsh_level_convertion_insert(njs_lvlhsh_query_t *lhq, void **parent,
    uint32_t key, njs_uint_t nlvl)
{
    void        **slot, **lvl;
    njs_int_t   ret;
    uintptr_t   mask;
    njs_uint_t  shift;

    shift = lhq->proto->shift[nlvl];
    mask = ((uintptr_t) 1 << shift) - 1;

    lvl = njs_lvlhsh_level(*parent, mask);
    slot = &lvl[key & mask];

    if (*slot == NULL) {
        ret = njs_lvlhsh_new_bucket(lhq, slot);

        if (njs_fast_path(ret == NJS_OK)) {
            njs_lvlhsh_count_inc(*parent);
        }

        return ret;
    }

    /* Only backets can be here. */

    return njs_lvlhsh_bucket_convertion_insert(lhq, slot, key >> shift, nlvl);
}


/*
 * The special bucket insertion procedure is required because during
 * convertion lhq->key contains garbage values and the test function
 * cannot be called.  Besides, the procedure can be simpler because
 * a new entry is inserted just after occupied entries.
 */

static njs_int_t
njs_lvlhsh_bucket_convertion_insert(njs_lvlhsh_query_t *lhq, void **slot,
    uint32_t key, njs_int_t nlvl)
{
    void                      **bkt;
    uint32_t                  *bucket, *e;
    njs_int_t                 ret;
    uintptr_t                 n;
    const njs_lvlhsh_proto_t  *proto;

    bkt = slot;
    proto = lhq->proto;

    do {
        bucket = njs_lvlhsh_bucket(proto, *bkt);
        n = njs_lvlhsh_bucket_entries(proto, *bkt);
        e = bucket + n * NJS_LVLHSH_ENTRY_SIZE;

        if (njs_fast_path(e < njs_lvlhsh_bucket_end(proto, bucket))) {

            njs_lvlhsh_set_entry_value(e, lhq->value);
            njs_lvlhsh_set_entry_key(e, lhq->key_hash);
            njs_lvlhsh_count_inc(*bkt);

            return NJS_OK;
        }

        bkt = njs_lvlhsh_next_bucket(proto, bucket);

    } while (*bkt != NULL);

    /* All buckets are full. */

    nlvl++;

    if (njs_fast_path(proto->shift[nlvl] != 0)) {

        ret = njs_lvlhsh_convert_bucket_to_level(lhq, slot, nlvl, bucket);

        if (njs_fast_path(ret == NJS_OK)) {
            return njs_lvlhsh_level_insert(lhq, slot, key, nlvl);
        }

        return ret;
    }

    /* The last allowed level, only buckets may be allocated here. */

    return njs_lvlhsh_new_bucket(lhq, bkt);
}


static njs_int_t
njs_lvlhsh_free_level(njs_lvlhsh_query_t *lhq, void **level, njs_uint_t size)
{
    size_t                    bsize;
    njs_uint_t                i;
    const njs_lvlhsh_proto_t  *proto;

    proto = lhq->proto;
    bsize = njs_lvlhsh_bucket_size(proto);

    for (i = 0; i < size; i++) {

        if (level[i] != NULL) {
            /*
             * Chained buckets are not possible here, since even
             * in the worst case one bucket cannot be converted
             * in two chained buckets but remains the same bucket.
             */
            proto->free(lhq->pool, njs_lvlhsh_bucket(proto, level[i]), bsize);
        }
    }

    proto->free(lhq->pool, level, size * (sizeof(void *)));

    return NJS_ERROR;
}


njs_int_t
njs_lvlhsh_delete(njs_lvlhsh_t *lh, njs_lvlhsh_query_t *lhq)
{
    if (njs_fast_path(lh->slot != NULL)) {

        if (njs_lvlhsh_is_bucket(lh->slot)) {
            return njs_lvlhsh_bucket_delete(lhq, &lh->slot);
        }

        return njs_lvlhsh_level_delete(lhq, &lh->slot, lhq->key_hash, 0);
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_lvlhsh_level_delete(njs_lvlhsh_query_t *lhq, void **parent, uint32_t key,
    njs_uint_t nlvl)
{
    size_t      size;
    void        **slot, **lvl;
    uintptr_t   mask;
    njs_int_t   ret;
    njs_uint_t  shift;

    shift = lhq->proto->shift[nlvl];
    mask = ((uintptr_t) 1 << shift) - 1;

    lvl = njs_lvlhsh_level(*parent, mask);
    slot = &lvl[key & mask];

    if (*slot != NULL) {

        if (njs_lvlhsh_is_bucket(*slot)) {
            ret = njs_lvlhsh_bucket_delete(lhq, slot);

        } else {
            key >>= shift;
            ret = njs_lvlhsh_level_delete(lhq, slot, key, nlvl + 1);
        }

        if (*slot == NULL) {
            njs_lvlhsh_count_dec(*parent);

            if (njs_lvlhsh_level_entries(*parent, mask) == 0) {
                *parent = NULL;
                size = njs_lvlhsh_level_size(lhq->proto, nlvl);
                lhq->proto->free(lhq->pool, lvl, size * sizeof(void *));
            }
        }

        return ret;
    }

    return NJS_DECLINED;
}


static njs_int_t
njs_lvlhsh_bucket_delete(njs_lvlhsh_query_t *lhq, void **bkt)
{
    void                      *value;
    size_t                    size;
    uint32_t                  *bucket, *e;
    uintptr_t                 n;
    const njs_lvlhsh_proto_t  *proto;

    proto = lhq->proto;

    do {
        bucket = njs_lvlhsh_bucket(proto, *bkt);
        n = njs_lvlhsh_bucket_entries(proto, *bkt);
        e = bucket;

        do {
            if (njs_lvlhsh_valid_entry(e)) {

                if (njs_lvlhsh_entry_key(e) == lhq->key_hash) {

                    value = njs_lvlhsh_entry_value(e);

                    if (proto->test(lhq, value) == NJS_OK) {

                        if (njs_lvlhsh_bucket_entries(proto, *bkt) == 1) {
                            *bkt = *njs_lvlhsh_next_bucket(proto, bucket);
                            size = njs_lvlhsh_bucket_size(proto);
                            proto->free(lhq->pool, bucket, size);

                        } else {
                            njs_lvlhsh_count_dec(*bkt);
                            njs_lvlhsh_set_entry_value(e, NULL);
                        }

                        lhq->value = value;

                        return NJS_OK;
                    }
                }

                n--;
            }

            e += NJS_LVLHSH_ENTRY_SIZE;

        } while (n != 0);

        bkt = njs_lvlhsh_next_bucket(proto, bucket);

    } while (*bkt != NULL);

    return NJS_DECLINED;
}


void *
njs_lvlhsh_each(const njs_lvlhsh_t *lh, njs_lvlhsh_each_t *lhe)
{
    void  **slot;

    if (lhe->bucket == NJS_LVLHSH_BUCKET_DONE) {
        slot = lh->slot;

        if (njs_lvlhsh_is_bucket(slot)) {
            return NULL;
        }

    } else {
        if (njs_slow_path(lhe->bucket == NULL)) {

            /* The first iteration only. */

            slot = lh->slot;

            if (slot == NULL) {
                return NULL;
            }

            if (!njs_lvlhsh_is_bucket(slot)) {
                goto level;
            }

            lhe->bucket = njs_lvlhsh_bucket(lhe->proto, slot);
            lhe->entries = njs_lvlhsh_bucket_entries(lhe->proto, slot);
        }

        return njs_lvlhsh_bucket_each(lhe);
    }

level:

    return njs_lvlhsh_level_each(lhe, slot, 0, 0);
}


static void *
njs_lvlhsh_level_each(njs_lvlhsh_each_t *lhe, void **level, njs_uint_t nlvl,
    njs_uint_t shift)
{
    void        **slot, *value;
    uintptr_t   mask;
    njs_uint_t  n, level_shift;

    level_shift = lhe->proto->shift[nlvl];
    mask = ((uintptr_t) 1 << level_shift) - 1;

    level = njs_lvlhsh_level(level, mask);

    do {
        n = (lhe->current >> shift) & mask;
        slot = level[n];

        if (slot != NULL) {
            if (njs_lvlhsh_is_bucket(slot)) {

                if (lhe->bucket != NJS_LVLHSH_BUCKET_DONE) {

                    lhe->bucket = njs_lvlhsh_bucket(lhe->proto, slot);
                    lhe->entries = njs_lvlhsh_bucket_entries(lhe->proto, slot);
                    lhe->entry = 0;

                    return njs_lvlhsh_bucket_each(lhe);
                }

                lhe->bucket = NULL;

            } else {
                value = njs_lvlhsh_level_each(lhe, slot, nlvl + 1,
                                              shift + level_shift);
                if (value != NULL) {
                    return value;
                }
            }
        }

        lhe->current &= ~(mask << shift);
        n = ((n + 1) & mask) << shift;
        lhe->current |= n;

    } while (n != 0);

    return NULL;
}


static void *
njs_lvlhsh_bucket_each(njs_lvlhsh_each_t *lhe)
{
    void      *value, **next;
    uint32_t  *bucket;

    /* At least one valid entry must present here. */
    do {
        bucket = &lhe->bucket[lhe->entry];
        lhe->entry += NJS_LVLHSH_ENTRY_SIZE;

    } while (njs_lvlhsh_free_entry(bucket));

    value = njs_lvlhsh_entry_value(bucket);
    lhe->key_hash = njs_lvlhsh_entry_key(bucket);

    lhe->entries--;

    if (lhe->entries == 0) {
        next = *njs_lvlhsh_next_bucket(lhe->proto, lhe->bucket);

        lhe->bucket = (next == NULL) ? NJS_LVLHSH_BUCKET_DONE
                                     : njs_lvlhsh_bucket(lhe->proto, next);

        lhe->entries = njs_lvlhsh_bucket_entries(lhe->proto, next);
        lhe->entry = 0;
    }

    return value;
}
