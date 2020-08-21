
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_UTILS_H_INCLUDED_
#define _NJS_UTILS_H_INCLUDED_


typedef int (*njs_sort_cmp_t)(const void *, const void *, void *ctx);

void njs_qsort(void *base, size_t n, size_t size, njs_sort_cmp_t cmp,
    void *ctx);

const char *njs_errno_string(int errnum);


njs_inline void
njs_swap_u8(void *a, void *b, size_t size)
{
    uint8_t  u, *au, *bu;

    au = (uint8_t *) a;
    bu = (uint8_t *) b;

    u = au[0];
    au[0] = bu[0];
    bu[0] = u;
}


njs_inline void
njs_swap_u16(void *a, void *b, size_t size)
{
    uint16_t  u, *au, *bu;

    au = (uint16_t *) a;
    bu = (uint16_t *) b;

    u = au[0];
    au[0] = bu[0];
    bu[0] = u;
}


njs_inline void
njs_swap_u32(void *a, void *b, size_t size)
{
    uint32_t  u, *au, *bu;

    au = (uint32_t *) a;
    bu = (uint32_t *) b;

    u = au[0];
    au[0] = bu[0];
    bu[0] = u;
}


njs_inline void
njs_swap_u64(void *a, void *b, size_t size)
{
    uint64_t  u, *au, *bu;

    au = (uint64_t *) a;
    bu = (uint64_t *) b;

    u = au[0];
    au[0] = bu[0];
    bu[0] = u;
}


#endif /* _NJS_UTILS_H_INCLUDED_ */
