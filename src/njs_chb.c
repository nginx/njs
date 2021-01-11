
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


#define NJS_CHB_MIN_SIZE       256


void
njs_chb_append0(njs_chb_t *chain, const char *msg, size_t len)
{
    u_char  *p;

    if (len != 0 && !chain->error) {
        p = njs_chb_reserve(chain, len);
        if (njs_slow_path(p == NULL)) {
            return;
        }

        memcpy(p, msg, len);

        njs_chb_written(chain, len);
    }
}


u_char *
njs_chb_reserve(njs_chb_t *chain, size_t size)
{
    njs_chb_node_t  *n;

    n = chain->last;

    if (njs_fast_path(n != NULL && njs_chb_node_room(n) >= size)) {
        return n->pos;
    }

    if (size < NJS_CHB_MIN_SIZE) {
        size = NJS_CHB_MIN_SIZE;
    }

    n = njs_mp_alloc(chain->pool, sizeof(njs_chb_node_t) + size);
    if (njs_slow_path(n == NULL)) {
        chain->error = 1;
        return NULL;
    }

    n->next = NULL;
    n->start = (u_char *) n + sizeof(njs_chb_node_t);
    n->pos = n->start;
    n->end = n->pos + size;

    if (chain->last != NULL) {
        chain->last->next = n;

    } else {
        chain->nodes = n;
    }

    chain->last = n;

    return n->start;
}


void
njs_chb_vsprintf(njs_chb_t *chain, size_t size, const char *fmt, va_list args)
{
    u_char  *start, *end;

    start = njs_chb_reserve(chain, size);
    if (njs_slow_path(start == NULL)) {
        return;
    }

    end = njs_vsprintf(start, start + size, fmt, args);

    njs_chb_written(chain, end - start);
}


void
njs_chb_sprintf(njs_chb_t *chain, size_t size, const char* fmt, ...)
{
    va_list  args;

    va_start(args, fmt);

    njs_chb_vsprintf(chain, size, fmt, args);

    va_end(args);
}


/*
 * Drains size bytes from the beginning of the chain.
 */
void
njs_chb_drain(njs_chb_t *chain, size_t drain)
{
    njs_chb_node_t  *n;

    n = chain->nodes;

    while (n != NULL) {
        if (njs_chb_node_size(n) > drain) {
            n->start += drain;
            return;
        }

        drain -= njs_chb_node_size(n);
        chain->nodes = n->next;

        njs_mp_free(chain->pool, n);
        n = chain->nodes;
    }

    chain->last = NULL;
}


/*
 * Drops size bytes from the end of the chain.
 */
void
njs_chb_drop(njs_chb_t *chain, size_t drop)
{
    uint64_t        size;
    njs_chb_node_t  *n, *next;

    if (njs_slow_path(chain->error)) {
        return;
    }

    n = chain->last;

    if (njs_fast_path(n != NULL && (njs_chb_node_size(n) > drop))) {
        n->pos -= drop;
        return;
    }

    n = chain->nodes;
    size = (uint64_t) njs_chb_size(chain);

    if (drop >= size) {
        njs_chb_destroy(chain);
        njs_chb_init(chain, chain->pool);
        return;
    }

    while (n != NULL) {
        size -= njs_chb_node_size(n);

        if (size <= drop) {
            chain->last = n;
            chain->last->pos -= drop - size;

            n = chain->last->next;
            chain->last->next = NULL;

            break;
        }

        n = n->next;
    }

    while (n != NULL) {
        next = n->next;
        njs_mp_free(chain->pool, n);
        n = next;
    }
}


njs_int_t
njs_chb_join(njs_chb_t *chain, njs_str_t *str)
{
    u_char          *start;
    uint64_t        size;
    njs_chb_node_t  *n;

    if (njs_slow_path(chain->error)) {
        return NJS_DECLINED;
    }

    n = chain->nodes;

    if (n == NULL) {
        str->length = 0;
        str->start = NULL;
        return NJS_OK;
    }

    size = (uint64_t) njs_chb_size(chain);
    if (njs_slow_path(size >= UINT32_MAX)) {
        return NJS_ERROR;
    }

    start = njs_mp_alloc(chain->pool, size);
    if (njs_slow_path(start == NULL)) {
        return NJS_ERROR;
    }

    str->length = size;
    str->start = start;

    njs_chb_join_to(chain, start);

    return NJS_OK;
}


void
njs_chb_join_to(njs_chb_t *chain, u_char *dst)
{
    njs_chb_node_t  *n;

    n = chain->nodes;

    while (n != NULL) {
        dst = njs_cpymem(dst, n->start, njs_chb_node_size(n));
        n = n->next;
    }
}


void
njs_chb_destroy(njs_chb_t *chain)
{
    njs_chb_node_t  *n, *next;

    n = chain->nodes;

    while (n != NULL) {
        next = n->next;
        njs_mp_free(chain->pool, n);
        n = next;
    }
}
