
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_LVLHSH_H_INCLUDED_
#define _NJS_LVLHSH_H_INCLUDED_


typedef struct njs_lvlhsh_query_s  njs_lvlhsh_query_t;

typedef njs_int_t (*njs_lvlhsh_test_t)(njs_lvlhsh_query_t *lhq, void *data);
typedef void *(*njs_lvlhsh_alloc_t)(void *ctx, size_t size);
typedef void (*njs_lvlhsh_free_t)(void *ctx, void *p, size_t size);


#if (NJS_64BIT)

#define NJS_LVLHSH_DEFAULT_BUCKET_SIZE  128
#define NJS_LVLHSH_ENTRY_SIZE           3

/* 3 is shift of 64-bit pointer. */
#define NJS_LVLHSH_MEMALIGN_SHIFT       (NJS_MAX_MEMALIGN_SHIFT - 3)

#else

#define NJS_LVLHSH_DEFAULT_BUCKET_SIZE  64
#define NJS_LVLHSH_ENTRY_SIZE           2

/* 2 is shift of 32-bit pointer. */
#define NJS_LVLHSH_MEMALIGN_SHIFT       (NJS_MAX_MEMALIGN_SHIFT - 2)

#endif


#if (NJS_LVLHSH_MEMALIGN_SHIFT < 10)
#define NJS_LVLHSH_MAX_MEMALIGN_SHIFT   NJS_LVLHSH_MEMALIGN_SHIFT
#else
#define NJS_LVLHSH_MAX_MEMALIGN_SHIFT   10
#endif


#define NJS_LVLHSH_BUCKET_END(bucket_size)                                    \
    (((bucket_size) - sizeof(void *))                                         \
        / (NJS_LVLHSH_ENTRY_SIZE * sizeof(uint32_t))                          \
     * NJS_LVLHSH_ENTRY_SIZE)


#define NJS_LVLHSH_BUCKET_SIZE(bucket_size)                                   \
    NJS_LVLHSH_BUCKET_END(bucket_size), bucket_size, (bucket_size - 1)


#define NJS_LVLHSH_DEFAULT                                                    \
    NJS_LVLHSH_BUCKET_SIZE(NJS_LVLHSH_DEFAULT_BUCKET_SIZE),                   \
    { 4, 4, 4, 4, 4, 4, 4, 0 }


#define NJS_LVLHSH_LARGE_SLAB                                                 \
    NJS_LVLHSH_BUCKET_SIZE(NJS_LVLHSH_DEFAULT_BUCKET_SIZE),                   \
    { 10, 4, 4, 4, 4, 4, 4, 0 }


#define NJS_LVLHSH_LARGE_MEMALIGN                                             \
    NJS_LVLHSH_BUCKET_SIZE(NJS_LVLHSH_DEFAULT_BUCKET_SIZE),                   \
    { NJS_LVLHSH_MAX_MEMALIGN_SHIFT, 4, 4, 4, 4, 0, 0, 0 }


typedef struct {
    uint32_t                  bucket_end;
    uint32_t                  bucket_size;
    uint32_t                  bucket_mask;
    uint8_t                   shift[8];

    njs_lvlhsh_test_t         test;
    njs_lvlhsh_alloc_t        alloc;
    njs_lvlhsh_free_t         free;
} njs_lvlhsh_proto_t;


typedef struct {
    void                      *slot;
} njs_lvlhsh_t;


struct njs_lvlhsh_query_s {
    uint32_t                  key_hash;
    njs_str_t                 key;

    uint8_t                   replace;     /* 1 bit */
    void                      *value;

    const njs_lvlhsh_proto_t  *proto;
    void                      *pool;

    /* Opaque data passed for the test function. */
    void                      *data;
};


#define njs_lvlhsh_is_empty(lh)                                               \
    ((lh)->slot == NULL)


#define njs_lvlhsh_init(lh)                                                   \
    (lh)->slot = NULL


#define njs_lvlhsh_eq(lhl, lhr)                                               \
    ((lhl)->slot == (lhr)->slot)

/*
 * njs_lvlhsh_find() finds a hash element.  If the element has been
 * found then it is stored in the lhq->value and njs_lvlhsh_find()
 * returns NJS_OK.  Otherwise NJS_DECLINED is returned.
 *
 * The required njs_lvlhsh_query_t fields: key_hash, key, proto.
 */
NJS_EXPORT njs_int_t njs_lvlhsh_find(const njs_lvlhsh_t *lh,
    njs_lvlhsh_query_t *lhq);

/*
 * njs_lvlhsh_insert() adds a hash element.  If the element already
 * presents in lvlhsh and the lhq->replace flag is zero, then lhq->value
 * is updated with the old element and NJS_DECLINED is returned.
 * If the element already presents in lvlhsh and the lhq->replace flag
 * is non-zero, then the old element is replaced with the new element.
 * lhq->value is updated with the old element, and NJS_OK is returned.
 * If the element is not present in lvlhsh, then it is inserted and
 * NJS_OK is returned.  The lhq->value is not changed.
 * On memory allocation failure NJS_ERROR is returned.
 *
 * The required njs_lvlhsh_query_t fields: key_hash, key, proto, replace, value.
 * The optional njs_lvlhsh_query_t fields: pool.
 */
NJS_EXPORT njs_int_t njs_lvlhsh_insert(njs_lvlhsh_t *lh,
    njs_lvlhsh_query_t *lhq);

/*
 * njs_lvlhsh_delete() deletes a hash element.  If the element has been
 * found then it is removed from lvlhsh and is stored in the lhq->value,
 * and NJS_OK is returned.  Otherwise NJS_DECLINED is returned.
 *
 * The required njs_lvlhsh_query_t fields: key_hash, key, proto.
 * The optional njs_lvlhsh_query_t fields: pool.
 */
NJS_EXPORT njs_int_t njs_lvlhsh_delete(njs_lvlhsh_t *lh,
    njs_lvlhsh_query_t *lhq);


typedef struct {
    const njs_lvlhsh_proto_t  *proto;

    /*
     * Fields to store current bucket entry position.  They cannot be
     * combined in a single bucket pointer with number of entries in low
     * bits, because entry positions are not aligned.  A current level is
     * stored as key bit path from the root.
     */
    uint32_t                  *bucket;
    uint32_t                  current;
    uint32_t                  entry;
    uint32_t                  entries;
    uint32_t                  key_hash;
} njs_lvlhsh_each_t;


#define njs_lvlhsh_each_init(lhe, _proto)                                     \
    do {                                                                      \
        njs_memzero(lhe, sizeof(njs_lvlhsh_each_t));                          \
        (lhe)->proto = _proto;                                                \
    } while (0)

NJS_EXPORT void *njs_lvlhsh_each(const njs_lvlhsh_t *lh,
    njs_lvlhsh_each_t *lhe);


#endif /* _NJS_LVLHSH_H_INCLUDED_ */
