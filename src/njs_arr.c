
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_auto_config.h>
#include <njs_types.h>
#include <njs_clang.h>
#include <njs_stub.h>
#include <njs_arr.h>
#include <njs_str.h>
#include <string.h>


njs_arr_t *
njs_arr_create(uint32_t items, uint32_t item_size,
    const njs_mem_proto_t *proto, void *pool)
{
    njs_arr_t  *array;

    array = proto->alloc(pool, sizeof(njs_arr_t) + items * item_size);

    if (njs_fast_path(array != NULL)) {
        array->start = (char *) array + sizeof(njs_arr_t);
        array->items = 0;
        array->item_size = item_size;
        array->avalaible = items;
        array->pointer = 1;
        array->separate = 1;
    }

    return array;
}


void *
njs_arr_init(njs_arr_t *array, void *start, uint32_t items,
    uint32_t item_size, const njs_mem_proto_t *proto, void *pool)
{
    array->start = start;
    array->items = items;
    array->item_size = item_size;
    array->avalaible = items;
    array->pointer = 0;
    array->separate = 0;

    if (array->start == NULL) {
        array->separate = 1;
        array->items = 0;

        array->start = proto->alloc(pool, items * item_size);
    }

    return array->start;
}


void
njs_arr_destroy(njs_arr_t *array, const njs_mem_proto_t *proto, void *pool)
{
    if (array->separate) {
        proto->free(pool, array->start);
#if (NJS_DEBUG)
        array->start = NULL;
        array->items = 0;
        array->avalaible = 0;
#endif
    }

    if (array->pointer) {
        proto->free(pool, array);
    }
}


void *
njs_arr_add(njs_arr_t *array, const njs_mem_proto_t *proto, void *pool)
{
    return njs_arr_add_multiple(array, proto, pool, 1);
}


void *
njs_arr_add_multiple(njs_arr_t *array, const njs_mem_proto_t *proto,
    void *pool, uint32_t items)
{
    void      *item, *start, *old;
    uint32_t  n;

    n = array->avalaible;
    items += array->items;

    if (items >= n) {

        if (n < 16) {
            /* Allocate new array twice as much as current. */
            n *= 2;

        } else {
            /* Allocate new array half as much as current. */
            n += n / 2;
        }

        if (n < items) {
            n = items;
        }

        start = proto->alloc(pool, n * array->item_size);
        if (njs_slow_path(start == NULL)) {
            return NULL;
        }

        array->avalaible = n;
        old = array->start;
        array->start = start;

        memcpy(start, old, (uint32_t) array->items * array->item_size);

        if (array->separate == 0) {
            array->separate = 1;

        } else {
            proto->free(pool, old);
        }
    }

    item = (char *) array->start + (uint32_t) array->items * array->item_size;

    array->items = items;

    return item;
}


void *
njs_arr_zero_add(njs_arr_t *array, const njs_mem_proto_t *proto, void *pool)
{
    void  *item;

    item = njs_arr_add(array, proto, pool);

    if (njs_fast_path(item != NULL)) {
        njs_memzero(item, array->item_size);
    }

    return item;
}


void
njs_arr_remove(njs_arr_t *array, void *item)
{
    u_char    *next, *last, *end;
    uint32_t  item_size;

    item_size = array->item_size;
    end = (u_char *) array->start + item_size * array->items;
    last = end - item_size;

    if (item != last) {
        next = (u_char *) item + item_size;

        memmove(item, next, end - next);
    }

    array->items--;
}
