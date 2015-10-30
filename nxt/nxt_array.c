
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <string.h>


nxt_array_t *
nxt_array_create(nxt_uint_t items, size_t item_size,
    const nxt_mem_proto_t *proto, void *pool)
{
    nxt_array_t  *array;

    array = proto->alloc(pool, sizeof(nxt_array_t) + items * item_size);

    if (nxt_fast_path(array != NULL)) {
        array->start = (char *) array + sizeof(nxt_array_t);
        array->items = 0;
        array->item_size = item_size;
        array->avalaible = items;
        array->type = NXT_ARRAY_EMBEDDED;
    }

    return array;
}


void *
nxt_array_init(nxt_array_t *array, nxt_uint_t items, size_t item_size,
    const nxt_mem_proto_t *proto, void *pool)
{
    array->start = proto->alloc(pool, items * item_size);

    if (nxt_fast_path(array->start != NULL)) {
        array->items = 0;
        array->item_size = item_size;
        array->avalaible = items;
        array->type = NXT_ARRAY_INITED;
    }

    return array->start;
}


void
nxt_array_destroy(nxt_array_t *array, const nxt_mem_proto_t *proto, void *pool)
{
    switch (array->type) {

    case NXT_ARRAY_INITED:
        proto->free(pool, array->start);
#if (NXT_DEBUG)
        array->start = NULL;
        array->items = 0;
        array->avalaible = 0;
#endif
        break;

    case NXT_ARRAY_DESCRETE:
        proto->free(pool, array->start);

        /* Fall through. */

    case NXT_ARRAY_EMBEDDED:
        proto->free(pool, array);
        break;
    }
}


void *
nxt_array_add(nxt_array_t *array, const nxt_mem_proto_t *proto, void *pool)
{
    void      *item, *start, *old;
    size_t    size;
    uint32_t  n;

    n = array->avalaible;

    if (n == array->items) {

        if (n < 16) {
            /* Allocate new array twice as much as current. */
            n *= 2;

        } else {
            /* Allocate new array half as much as current. */
            n += n / 2;
        }

        size = n * array->item_size;

        start = proto->alloc(pool, size);
        if (nxt_slow_path(start == NULL)) {
            return NULL;
        }

        array->avalaible = n;
        old = array->start;
        array->start = start;

        memcpy(start, old, size);

        if (array->type == NXT_ARRAY_EMBEDDED) {
            array->type = NXT_ARRAY_DESCRETE;

        } else {
            proto->free(pool, old);
        }
    }

    item = (char *) array->start + array->item_size * array->items;

    array->items++;

    return item;
}


void *
nxt_array_zero_add(nxt_array_t *array, const nxt_mem_proto_t *proto, void *pool)
{
    void  *item;

    item = nxt_array_add(array, proto, pool);

    if (nxt_fast_path(item != NULL)) {
        memset(item, 0, array->item_size);
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
