
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef void (*njs_swap_t) (void *a, void *b, size_t size);


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


njs_inline void
njs_swap_u128(void *a, void *b, size_t size)
{
    uint64_t  u, v, *au, *bu;

    au = (uint64_t *) a;
    bu = (uint64_t *) b;

    u = au[0];
    v = au[1];
    au[0] = bu[0];
    au[1] = bu[1];
    bu[0] = u;
    bu[1] = v;
}


njs_inline void
njs_swap_u128x(void *a, void *b, size_t size)
{
    uint64_t  u, v, *au, *bu;

    au = (uint64_t *) a;
    bu = (uint64_t *) b;

    do {
        u = au[0];
        v = au[1];
        au[0] = bu[0];
        au[1] = bu[1];
        bu[0] = u;
        bu[1] = v;

        size -= sizeof(uint64_t) * 2;

        au += 2;
        bu += 2;
    } while (size != 0);
}


njs_inline void
njs_swap_bytes(void *a, void *b, size_t size)
{
    uint8_t  u, *au, *bu;

    au = (uint8_t *) a;
    bu = (uint8_t *) b;

    while (size-- != 0) {
        u = *au;
        *au++ = *bu;
        *bu++ = u;
    }
}


njs_inline njs_swap_t
njs_choose_swap(size_t size)
{
    switch (size) {
    case 2:
        return njs_swap_u16;
    case 4:
        return njs_swap_u32;
    case 8:
        return njs_swap_u64;
    case 16:
        return njs_swap_u128;
    default:
        if ((size % 16) == 0) {
            return njs_swap_u128x;
        }

        if (size == 1) {
            return njs_swap_u8;
        }
    }

    return njs_swap_bytes;
}


njs_inline void
njs_sift_down(u_char *base, njs_sort_cmp_t cmp, njs_swap_t swap, size_t n,
    size_t esize, void *ctx, njs_uint_t i)
{
    njs_uint_t  c, m;

    m = i;

    while (1) {
        c = 2 * i + esize;

        if (c < n && cmp(base + m, base + c, ctx) < 0) {
            m = c;
        }

        c += esize;

        if (c < n && cmp(base + m, base + c, ctx) < 0) {
            m = c;
        }

        if (m == i) {
            break;
        }

        swap(base + i, base + m, esize);
        i = m;
    }
}


static void
njs_heapsort(u_char *base, size_t n, size_t esize, njs_swap_t swap,
    njs_sort_cmp_t cmp, void *ctx)
{
    njs_uint_t  i;

    i = (n / 2) * esize;
    n = n * esize;

    for ( ;; ) {
        njs_sift_down(base, cmp, swap, n, esize, ctx, i);

        if (i == 0) {
            break;
        }

        i -= esize;
    }

    while (n > esize) {
        swap(base, base + n - esize, esize);
        n -= esize;

        njs_sift_down(base, cmp, swap, n, esize, ctx, 0);
    }
}


njs_inline void *
njs_pivot(void *a, void *b, void *c, njs_sort_cmp_t cmp, void *ctx)
{
    if (cmp(a, c, ctx) < 0) {
        if (cmp(b, c, ctx) < 0) {
            return (cmp(a, b, ctx) < 0) ? b : a;
        }

        return c;
    }

    if (cmp(b, a, ctx) < 0) {
        return (cmp(b, c, ctx) < 0) ? c : b;
    }

    return a;
}


typedef struct {
    u_char      *base;
    njs_uint_t  elems;
} njs_qsort_state_t;


#define NJS_MAX_DEPTH  16


void
njs_qsort(void *arr, size_t n, size_t esize, njs_sort_cmp_t cmp, void *ctx)
{
    int                r;
    u_char             *base, *lt, *gt, *p, *end;
    njs_uint_t         m4;
    njs_swap_t         swap;
    njs_qsort_state_t  stack[NJS_MAX_DEPTH], *sp;

    if (n < 2) {
        return;
    }

    swap = njs_choose_swap(esize);

    sp = stack;
    sp->base = arr;
    sp->elems = n;
    sp++;

    while (sp-- > stack) {
        base = sp->base;
        n = sp->elems;
        end = base + n * esize;

        while (n > 6) {
            if (njs_slow_path(sp == &stack[NJS_MAX_DEPTH - 1])) {
                njs_heapsort(base, n, esize, swap, cmp, ctx);
                end = base;
                break;
            }

            m4 = (n / 4) * esize;
            p = njs_pivot(base + m4, base + 2 * m4, base + 3 * m4, cmp, ctx);
            swap(base, p, esize);

            /**
             * Partition
             *  < mid | == mid | unprocessed | mid >
             *        lt       p             gt
             */

            lt = base;
            gt = end;
            p = lt + esize;

            while (p < gt) {
                r = cmp(p, lt, ctx);

                if (r <= 0) {
                    if (r < 0) {
                        swap(lt, p, esize);
                        lt += esize;
                    }

                    p += esize;
                    continue;
                }

                swap(gt - esize, p, esize);
                gt -= esize;
            }

            if (lt - base > end - gt) {
                sp->base = base;
                sp->elems = (lt - base) / esize;

                base = gt;
                n = (end - gt) / esize;

            } else {
                sp->base = gt;
                sp->elems = (end - gt) / esize;

                n = (lt - base) / esize;
            }

            end = base + n * esize;
            sp++;
        }

        /* Insertion sort. */

        for (p = base + esize; p < end; p += esize) {
            while (p > base && cmp(p, p - esize, ctx) < 0) {
                swap(p, p - esize, esize);
                p -= esize;
            }
        }
    }
}
