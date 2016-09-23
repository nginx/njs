
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_ARRAY_H_INCLUDED_
#define _NXT_ARRAY_H_INCLUDED_


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
} nxt_array_t;


NXT_EXPORT nxt_array_t *nxt_array_create(uint32_t items, uint32_t item_size,
    const nxt_mem_proto_t *proto, void *pool);
NXT_EXPORT void *nxt_array_init(nxt_array_t *array, void *start,
    uint32_t items, uint32_t item_size, const nxt_mem_proto_t *proto,
    void *pool);
NXT_EXPORT void nxt_array_destroy(nxt_array_t *array,
    const nxt_mem_proto_t *proto, void *pool);
NXT_EXPORT void *nxt_array_add(nxt_array_t *array, const nxt_mem_proto_t *proto,
    void *pool);
NXT_EXPORT void *nxt_array_add_multiple(nxt_array_t *array,
    const nxt_mem_proto_t *proto, void *pool, uint32_t items);
NXT_EXPORT void *nxt_array_zero_add(nxt_array_t *array,
    const nxt_mem_proto_t *proto, void *pool);
NXT_EXPORT void nxt_array_remove(nxt_array_t *array, void *item);


#define nxt_array_last(array)                                                 \
    ((void *)                                                                 \
        ((char *) (array)->start                                              \
                      + (array)->item_size * ((array)->items - 1)))


#define nxt_array_reset(array)                                                \
    (array)->items = 0;


#define nxt_array_is_empty(array)                                             \
    ((array)->items == 0)


nxt_inline void *
nxt_array_remove_last(nxt_array_t *array)
{
    array->items--;
    return (char *) array->start + array->item_size * array->items;
}


#endif /* _NXT_ARRAY_H_INCLUDED_ */
