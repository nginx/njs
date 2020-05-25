
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ARR_H_INCLUDED_
#define _NJS_ARR_H_INCLUDED_


typedef struct {
    void              *start;
    /*
     * A array can hold no more than 2**32 items.
     * the item size is no more than 64K.
     */
    uint32_t          items;
    uint32_t          available;
    uint16_t          item_size;

    uint8_t           pointer;
    uint8_t           separate;
    njs_mp_t          *mem_pool;
} njs_arr_t;


NJS_EXPORT njs_arr_t *njs_arr_create(njs_mp_t *mp, njs_uint_t n,
    size_t size);
NJS_EXPORT void *njs_arr_init(njs_mp_t *mp, njs_arr_t *arr, void *start,
    njs_uint_t n, size_t size);
NJS_EXPORT void njs_arr_destroy(njs_arr_t *arr);
NJS_EXPORT void *njs_arr_add(njs_arr_t *arr);
NJS_EXPORT void *njs_arr_add_multiple(njs_arr_t *arr, njs_uint_t n);
NJS_EXPORT void *njs_arr_zero_add(njs_arr_t *arr);
NJS_EXPORT void njs_arr_remove(njs_arr_t *arr, void *item);


#define njs_arr_item(arr, i)                                                \
    ((void *) ((char *) (arr)->start + (arr)->item_size * (i)))


#define njs_arr_last(arr)                                                   \
    ((void *)                                                               \
        ((char *) (arr)->start                                              \
                      + (arr)->item_size * ((arr)->items - 1)))


#define njs_arr_reset(arr)                                                  \
    (arr)->items = 0;


#define njs_arr_is_empty(arr)                                               \
    ((arr)->items == 0)


njs_inline void *
njs_arr_remove_last(njs_arr_t *arr)
{
    arr->items--;
    return (char *) arr->start + arr->item_size * arr->items;
}


#endif /* _NJS_ARR_H_INCLUDED_ */
