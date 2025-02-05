
/*
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_FLATHSH_OBJ_H_INCLUDED_
#define _NJS_FLATHSH_OBJ_H_INCLUDED_


typedef struct {
    void         *slot;
} njs_flathsh_obj_t;


typedef struct {
    uint32_t     next_elt;
    uint32_t     key_hash;
    void         *value;
} njs_flathsh_obj_elt_t;


typedef struct njs_flathsh_obj_descr_s  njs_flathsh_obj_descr_t;
typedef struct njs_flathsh_obj_query_s  njs_flathsh_obj_query_t;

typedef njs_int_t (*njs_flathsh_obj_test_t)(njs_flathsh_obj_query_t *fhq,
                                            void *data);
typedef void *(*njs_flathsh_obj_alloc_t)(void *ctx, size_t size);
typedef void (*njs_flathsh_obj_free_t)(void *ctx, void *p, size_t size);

typedef struct njs_flathsh_obj_proto_s  njs_flathsh_obj_proto_t;


struct njs_flathsh_obj_descr_s {
    uint32_t     hash_mask;
    uint32_t     elts_size;          /* allocated properties */
    uint32_t     elts_count;         /* include deleted properties */
    uint32_t     elts_deleted_count;
};


struct njs_flathsh_obj_proto_s {
    njs_flathsh_obj_alloc_t        alloc;
    njs_flathsh_obj_free_t         free;
};


struct njs_flathsh_obj_query_s {
    uint32_t                       key_hash;
    njs_str_t                      key;

    uint8_t                        replace;     /* 1 bit */
    void                           *value;

    const njs_flathsh_obj_proto_t  *proto;
    void                           *pool;

    /* Opaque data passed for the test function. */
    void                           *data;
};


#define njs_flathsh_obj_is_empty(fh)                                           \
    ((fh)->slot == NULL)


#define njs_flathsh_obj_init(fh)                                               \
    (fh)->slot = NULL


#define njs_flathsh_obj_eq(fhl, fhr)                                           \
    ((fhl)->slot == (fhr)->slot)


njs_inline uint32_t *
njs_flathsh_obj_hash_cells_end(njs_flathsh_obj_descr_t *h)
{
    return (uint32_t *) h;
}


njs_inline njs_flathsh_obj_elt_t *
njs_flathsh_obj_hash_elts(njs_flathsh_obj_descr_t *h)
{
    return (njs_flathsh_obj_elt_t *) ((char *) h +
        sizeof(njs_flathsh_obj_descr_t));
}


/*
 * njs_flathsh_obj_find() finds a hash element.  If the element has been
 * found then it is stored in the fhq->value and njs_flathsh_obj_find()
 * returns NJS_OK.  Otherwise NJS_DECLINED is returned.
 *
 * The required njs_flathsh_obj_query_t fields: key_hash, key, proto.
 */
NJS_EXPORT njs_int_t njs_flathsh_obj_find(const njs_flathsh_obj_t *fh,
    njs_flathsh_obj_query_t *fhq);

/*
 * njs_flathsh_obj_insert() adds a hash element.  If the element is already
 * present in flathsh and the fhq->replace flag is zero, then fhq->value
 * is updated with the old element and NJS_DECLINED is returned.
 * If the element is already present in flathsh and the fhq->replace flag
 * is non-zero, then the old element is replaced with the new element.
 * fhq->value is updated with the old element, and NJS_OK is returned.
 * If the element is not present in flathsh, then it is inserted and
 * NJS_OK is returned.  The fhq->value is not changed.
 * On memory allocation failure NJS_ERROR is returned.
 *
 * The required njs_flathsh_obj_query_t fields: key_hash, key, proto, replace,
 * value.
 * The optional njs_flathsh_obj_query_t fields: pool.
 */
NJS_EXPORT njs_int_t njs_flathsh_obj_insert(njs_flathsh_obj_t *fh,
    njs_flathsh_obj_query_t *fhq);

/*
 * njs_flathsh_obj_delete() deletes a hash element.  If the element has been
 * found then it is removed from flathsh and is stored in the fhq->value,
 * and NJS_OK is returned.  Otherwise NJS_DECLINED is returned.
 *
 * The required njs_flathsh_obj_query_t fields: key_hash, key, proto.
 * The optional njs_flathsh_obj_query_t fields: pool.
 */
NJS_EXPORT njs_int_t njs_flathsh_obj_delete(njs_flathsh_obj_t *fh,
    njs_flathsh_obj_query_t *fhq);


typedef struct {
    uint32_t                   cp;
} njs_flathsh_obj_each_t;


#define njs_flathsh_obj_each_init(lhe, _proto)                                 \
    do {                                                                       \
        njs_memzero(lhe, sizeof(njs_flathsh_obj_each_t));                      \
    } while (0)


NJS_EXPORT njs_flathsh_obj_elt_t *njs_flathsh_obj_each(
    const njs_flathsh_obj_t *fh, njs_flathsh_obj_each_t *fhe);

/*
 * Add element into hash.
 * The element value is not initialized.
 * Returns NULL if memory error in hash expand.
 */
NJS_EXPORT njs_flathsh_obj_elt_t *njs_flathsh_obj_add_elt(njs_flathsh_obj_t *fh,
    njs_flathsh_obj_query_t *fhq);

NJS_EXPORT njs_flathsh_obj_descr_t *njs_flathsh_obj_new(
    njs_flathsh_obj_query_t *fhq);
NJS_EXPORT void njs_flathsh_obj_destroy(njs_flathsh_obj_t *fh,
    njs_flathsh_obj_query_t *fhq);
NJS_EXPORT njs_flathsh_obj_descr_t * njs_flathsh_obj_expand_elts(
    njs_flathsh_obj_query_t *fhq, njs_flathsh_obj_descr_t *h);

NJS_EXPORT njs_int_t njs_flathsh_obj_alloc_copy(njs_mp_t *mp,
    njs_flathsh_obj_t *to, njs_flathsh_obj_t *from);


#endif /* _NJS_FLATHSH_OBJ_H_INCLUDED_ */
