
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_CHB_H_INCLUDED_
#define _NJS_CHB_H_INCLUDED_


typedef struct njs_chb_node_s njs_chb_node_t;

struct njs_chb_node_s {
    njs_chb_node_t          *next;
    u_char                  *start;
    u_char                  *pos;
    u_char                  *end;
};

typedef void *(*njs_chb_alloc_t)(void *pool, size_t size);
typedef void (*njs_chb_free_t)(void *pool, void *p);

typedef struct {
    njs_bool_t              error;

    void                    *pool;
    njs_chb_alloc_t         alloc;
    njs_chb_free_t          free;

    njs_chb_node_t          *nodes;
    njs_chb_node_t          *last;
} njs_chb_t;


void njs_chb_init(njs_chb_t *chain, void *pool, njs_chb_alloc_t alloc,
    njs_chb_free_t free);
#define NJS_CHB_MP_INIT(chain, vm)                                           \
    njs_chb_init(chain, njs_vm_memory_pool(vm), (njs_chb_alloc_t) njs_mp_alloc,\
    (njs_chb_free_t) njs_mp_free)
#define NJS_CHB_CTX_INIT(chain, ctx)                                         \
    njs_chb_init(chain, ctx, (njs_chb_alloc_t) js_malloc,                    \
    (njs_chb_free_t) js_free)
void njs_chb_append0(njs_chb_t *chain, const char *msg, size_t len);
void njs_chb_vsprintf(njs_chb_t *chain, size_t size, const char *fmt,
    va_list args);
void njs_chb_sprintf(njs_chb_t *chain, size_t size, const char* fmt, ...);
u_char *njs_chb_reserve(njs_chb_t *chain, size_t size);
void njs_chb_drain(njs_chb_t *chain, size_t drop);
void njs_chb_drop(njs_chb_t *chain, size_t drop);
njs_int_t njs_chb_join(njs_chb_t *chain, njs_str_t *str);
void njs_chb_join_to(njs_chb_t *chain, u_char *dst);
void njs_chb_destroy(njs_chb_t *chain);


#define njs_chb_append(chain, msg, len)                                      \
    njs_chb_append0(chain, (const char *) (msg), len)

#define njs_chb_append_literal(chain, literal)                               \
    njs_chb_append0(chain, literal, njs_length(literal))


#define njs_chb_node_size(n) (size_t) ((n)->pos - (n)->start)
#define njs_chb_node_room(n) (size_t) ((n)->end - (n)->pos)


njs_inline void
njs_chb_append_str(njs_chb_t *chain, njs_str_t *str)
{
    njs_chb_append0(chain, (const char *) str->start, str->length);
}


njs_inline int64_t
njs_chb_size(njs_chb_t *chain)
{
    uint64_t        size;
    njs_chb_node_t  *n;

    if (njs_slow_path(chain->error)) {
        return -1;
    }

    n = chain->nodes;

    size = 0;

    while (n != NULL) {
        size += njs_chb_node_size(n);
        n = n->next;
    }

    return size;
}


njs_inline int64_t
njs_chb_utf8_length(njs_chb_t *chain)
{
    u_char          *p, *p_end;
    size_t          size;
    int64_t         len, length;
    njs_chb_node_t  *n;

    if (njs_slow_path(chain->error)) {
        return -1;
    }

    n = chain->nodes;

    length = 0;

    while (n != NULL) {
        p = n->start;
        size = njs_chb_node_size(n);
        p_end = p + size;

        while (p < p_end && *p < 0x80) {
              p++;
        }

        if (p != p_end) {
            break;
        }

        length += size;
        n = n->next;
    }

    while (n != NULL) {
        len = njs_utf8_length(n->start, njs_chb_node_size(n));
        if (njs_slow_path(len < 0)) {
            return -1;
        }

        length += len;
        n = n->next;
    }

    return length;
}


njs_inline u_char *
njs_chb_current(njs_chb_t *chain)
{
    return (chain->last != NULL) ? chain->last->pos : NULL;
}


njs_inline void
njs_chb_written(njs_chb_t *chain, size_t bytes)
{
    chain->last->pos += bytes;
}


#endif /* _NJS_JSON_H_INCLUDED_ */
