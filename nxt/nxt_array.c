
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_string.h>
#include <string.h>


nxt_array_t *
nxt_array_create(uint32_t items, uint32_t item_size,
    const nxt_mem_proto_t *proto, void *pool)
{
    nxt_array_t  *array;

    array = proto->alloc(pool, sizeof(nxt_array_t) + items * item_size);

    if (nxt_fast_path(array != NULL)) {
        array->start = (char *) array + sizeof(nxt_array_t);
        array->items = 0;
        array->item_size = item_size;
        array->avalaible = items;
        array->pointer = 1;
        array->separate = 1;
    }

    return array;
}


void *
nxt_array_init(nxt_array_t *array, void *start, uint32_t items,
    uint32_t item_size, const nxt_mem_proto_t *proto, void *pool)
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
nxt_array_destroy(nxt_array_t *array, const nxt_mem_proto_t *proto, void *pool)
{
    if (array->separate) {
        proto->free(pool, array->start);
#if (NXT_DEBUG)
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
nxt_array_add(nxt_array_t *array, const nxt_mem_proto_t *proto, void *pool)
{
    return nxt_array_add_multiple(array, proto, pool, 1);
}


void *
nxt_array_add_multiple(nxt_array_t *array, const nxt_mem_proto_t *proto,
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
        if (nxt_slow_path(start == NULL)) {
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
nxt_array_zero_add(nxt_array_t *array, const nxt_mem_proto_t *proto, void *pool)
{
    void  *item;

    item = nxt_array_add(array, proto, pool);

    if (nxt_fast_path(item != NULL)) {
        nxt_memzero(item, array->item_size);
    }

    return item;
}


void
nxt_array_remove(nxt_array_t *array, void *item)
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
