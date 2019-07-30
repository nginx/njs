
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_ARR_H_INCLUDED_
#define _NJS_ARR_H_INCLUDED_


typedef struct {
    void              *start;
    /*
     * A array can hold no more than 65536 items.
     * The item size is no more than 64K.
     */
    uint16_t          items;
    uint16_t          avalaible;
    uint16_t          item_size;

    uint8_t           pointer;
    uint8_t           separate;
} njs_arr_t;


NJS_EXPORT njs_arr_t *njs_arr_create(uint32_t items, uint32_t item_size,
    const njs_mem_proto_t *proto, void *pool);
NJS_EXPORT void *njs_arr_init(njs_arr_t *array, void *start, uint32_t items,
    uint32_t item_size, const njs_mem_proto_t *proto, void *pool);
NJS_EXPORT void njs_arr_destroy(njs_arr_t *array,
    const njs_mem_proto_t *proto, void *pool);
NJS_EXPORT void *njs_arr_add(njs_arr_t *array, const njs_mem_proto_t *proto,
    void *pool);
NJS_EXPORT void *njs_arr_add_multiple(njs_arr_t *array,
    const njs_mem_proto_t *proto, void *pool, uint32_t items);
NJS_EXPORT void *njs_arr_zero_add(njs_arr_t *array,
    const njs_mem_proto_t *proto, void *pool);
NJS_EXPORT void njs_arr_remove(njs_arr_t *array, void *item);


#define njs_arr_item(array, i)                                              \
    ((void *) ((char *) (array)->start + (array)->item_size * (i)))


#define njs_arr_last(array)                                                 \
    ((void *)                                                                 \
        ((char *) (array)->start                                              \
                      + (array)->item_size * ((array)->items - 1)))


#define njs_arr_reset(array)                                                \
    (array)->items = 0;


#define njs_arr_is_empty(array)                                             \
    ((array)->items == 0)


njs_inline void *
njs_arr_remove_last(njs_arr_t *array)
{
    array->items--;
    return (char *) array->start + array->item_size * array->items;
}


#endif /* _NJS_ARR_H_INCLUDED_ */
