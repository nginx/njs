
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


njs_arr_t *
njs_arr_create(njs_mp_t *mp, njs_uint_t n, size_t size)
{
    njs_arr_t  *arr;

    arr = njs_mp_alloc(mp, sizeof(njs_arr_t) + n * size);
    if (njs_slow_path(arr == NULL)) {
        return NULL;
    }

    arr->start = (char *) arr + sizeof(njs_arr_t);
    arr->items = 0;
    arr->item_size = size;
    arr->available = n;
    arr->pointer = 1;
    arr->separate = 0;
    arr->mem_pool = mp;

    return arr;
}


void *
njs_arr_init(njs_mp_t *mp, njs_arr_t *arr, void *start, njs_uint_t n,
    size_t size)
{
    arr->start = start;
    arr->items = n;
    arr->item_size = size;
    arr->available = n;
    arr->pointer = 0;
    arr->separate = 0;
    arr->mem_pool = mp;

    if (arr->start == NULL) {
        arr->separate = 1;
        arr->items = 0;

        arr->start = njs_mp_alloc(mp, n * size);
    }

    return arr->start;
}


void
njs_arr_destroy(njs_arr_t *arr)
{
    if (arr->separate) {
        njs_mp_free(arr->mem_pool, arr->start);
#if (NJS_DEBUG)
        arr->start = NULL;
        arr->items = 0;
        arr->available = 0;
#endif
    }

    if (arr->pointer) {
        njs_mp_free(arr->mem_pool, arr);
    }
}


void *
njs_arr_add(njs_arr_t *arr)
{
    return njs_arr_add_multiple(arr, 1);
}


void *
njs_arr_add_multiple(njs_arr_t *arr, njs_uint_t items)
{
    void      *item, *start, *old;
    uint32_t  n;

    n = arr->available;
    items += arr->items;

    if (items >= n) {

        if (n < 16) {
            /* Allocate new arr twice as much as current. */
            n *= 2;

        } else {
            /* Allocate new arr half as much as current. */
            n += n / 2;
        }

        if (n < items) {
            n = items;
        }

        start = njs_mp_alloc(arr->mem_pool, n * arr->item_size);
        if (njs_slow_path(start == NULL)) {
            return NULL;
        }

        arr->available = n;
        old = arr->start;
        arr->start = start;

        memcpy(start, old, arr->items * arr->item_size);

        if (arr->separate == 0) {
            arr->separate = 1;

        } else {
            njs_mp_free(arr->mem_pool, old);
        }
    }

    item = (char *) arr->start + arr->items * arr->item_size;

    arr->items = items;

    return item;
}


void *
njs_arr_zero_add(njs_arr_t *arr)
{
    void  *item;

    item = njs_arr_add(arr);

    if (njs_fast_path(item != NULL)) {
        njs_memzero(item, arr->item_size);
    }

    return item;
}


void
njs_arr_remove(njs_arr_t *arr, void *item)
{
    u_char    *next, *last, *end;
    uint32_t  item_size;

    item_size = arr->item_size;
    end = (u_char *) arr->start + item_size * arr->items;
    last = end - item_size;

    if (item != last) {
        next = (u_char *) item + item_size;

        memmove(item, next, end - next);
    }

    arr->items--;
}
