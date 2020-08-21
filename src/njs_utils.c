
/*
 * Copyright (C) Dmitry Volyntsev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef void (*njs_swap_t) (void *a, void *b, size_t size);


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
    u_char             *base, *lt, *gt, *c, *p, *end;
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
            for (c = p; c > base && cmp(c, c - esize, ctx) < 0; c -= esize) {
                swap(c, c - esize, esize);
            }
        }
    }
}


#define njs_errno_case(e)                                                   \
    case e:                                                                 \
        return #e;


const char*
njs_errno_string(int errnum)
{
    switch (errnum) {
#ifdef EACCES
    njs_errno_case(EACCES);
#endif

#ifdef EADDRINUSE
    njs_errno_case(EADDRINUSE);
#endif

#ifdef EADDRNOTAVAIL
    njs_errno_case(EADDRNOTAVAIL);
#endif

#ifdef EAFNOSUPPORT
    njs_errno_case(EAFNOSUPPORT);
#endif

#ifdef EAGAIN
    njs_errno_case(EAGAIN);
#endif

#ifdef EWOULDBLOCK
#if EAGAIN != EWOULDBLOCK
    njs_errno_case(EWOULDBLOCK);
#endif
#endif

#ifdef EALREADY
    njs_errno_case(EALREADY);
#endif

#ifdef EBADF
    njs_errno_case(EBADF);
#endif

#ifdef EBADMSG
    njs_errno_case(EBADMSG);
#endif

#ifdef EBUSY
    njs_errno_case(EBUSY);
#endif

#ifdef ECANCELED
    njs_errno_case(ECANCELED);
#endif

#ifdef ECHILD
    njs_errno_case(ECHILD);
#endif

#ifdef ECONNABORTED
    njs_errno_case(ECONNABORTED);
#endif

#ifdef ECONNREFUSED
    njs_errno_case(ECONNREFUSED);
#endif

#ifdef ECONNRESET
    njs_errno_case(ECONNRESET);
#endif

#ifdef EDEADLK
    njs_errno_case(EDEADLK);
#endif

#ifdef EDESTADDRREQ
    njs_errno_case(EDESTADDRREQ);
#endif

#ifdef EDOM
    njs_errno_case(EDOM);
#endif

#ifdef EDQUOT
    njs_errno_case(EDQUOT);
#endif

#ifdef EEXIST
    njs_errno_case(EEXIST);
#endif

#ifdef EFAULT
    njs_errno_case(EFAULT);
#endif

#ifdef EFBIG
    njs_errno_case(EFBIG);
#endif

#ifdef EHOSTUNREACH
    njs_errno_case(EHOSTUNREACH);
#endif

#ifdef EIDRM
    njs_errno_case(EIDRM);
#endif

#ifdef EILSEQ
    njs_errno_case(EILSEQ);
#endif

#ifdef EINPROGRESS
    njs_errno_case(EINPROGRESS);
#endif

#ifdef EINTR
    njs_errno_case(EINTR);
#endif

#ifdef EINVAL
    njs_errno_case(EINVAL);
#endif

#ifdef EIO
    njs_errno_case(EIO);
#endif

#ifdef EISCONN
    njs_errno_case(EISCONN);
#endif

#ifdef EISDIR
    njs_errno_case(EISDIR);
#endif

#ifdef ELOOP
    njs_errno_case(ELOOP);
#endif

#ifdef EMFILE
    njs_errno_case(EMFILE);
#endif

#ifdef EMLINK
    njs_errno_case(EMLINK);
#endif

#ifdef EMSGSIZE
    njs_errno_case(EMSGSIZE);
#endif

#ifdef EMULTIHOP
    njs_errno_case(EMULTIHOP);
#endif

#ifdef ENAMETOOLONG
    njs_errno_case(ENAMETOOLONG);
#endif

#ifdef ENETDOWN
    njs_errno_case(ENETDOWN);
#endif

#ifdef ENETRESET
    njs_errno_case(ENETRESET);
#endif

#ifdef ENETUNREACH
    njs_errno_case(ENETUNREACH);
#endif

#ifdef ENFILE
    njs_errno_case(ENFILE);
#endif

#ifdef ENOBUFS
    njs_errno_case(ENOBUFS);
#endif

#ifdef ENODATA
    njs_errno_case(ENODATA);
#endif

#ifdef ENODEV
    njs_errno_case(ENODEV);
#endif

#ifdef ENOENT
    njs_errno_case(ENOENT);
#endif

#ifdef ENOEXEC
    njs_errno_case(ENOEXEC);
#endif

#ifdef ENOLINK
    njs_errno_case(ENOLINK);
#endif

#ifdef ENOLCK
#if ENOLINK != ENOLCK
    njs_errno_case(ENOLCK);
#endif
#endif

#ifdef ENOMEM
    njs_errno_case(ENOMEM);
#endif

#ifdef ENOMSG
    njs_errno_case(ENOMSG);
#endif

#ifdef ENOPROTOOPT
    njs_errno_case(ENOPROTOOPT);
#endif

#ifdef ENOSPC
    njs_errno_case(ENOSPC);
#endif

#ifdef ENOSR
    njs_errno_case(ENOSR);
#endif

#ifdef ENOSTR
    njs_errno_case(ENOSTR);
#endif

#ifdef ENOSYS
    njs_errno_case(ENOSYS);
#endif

#ifdef ENOTCONN
    njs_errno_case(ENOTCONN);
#endif

#ifdef ENOTDIR
    njs_errno_case(ENOTDIR);
#endif

#ifdef ENOTEMPTY
#if ENOTEMPTY != EEXIST
    njs_errno_case(ENOTEMPTY);
#endif
#endif

#ifdef ENOTSOCK
    njs_errno_case(ENOTSOCK);
#endif

#ifdef ENOTSUP
    njs_errno_case(ENOTSUP);
#else
#ifdef EOPNOTSUPP
    njs_errno_case(EOPNOTSUPP);
#endif
#endif

#ifdef ENOTTY
    njs_errno_case(ENOTTY);
#endif

#ifdef ENXIO
    njs_errno_case(ENXIO);
#endif

#ifdef EOVERFLOW
    njs_errno_case(EOVERFLOW);
#endif

#ifdef EPERM
    njs_errno_case(EPERM);
#endif

#ifdef EPIPE
    njs_errno_case(EPIPE);
#endif

#ifdef EPROTO
    njs_errno_case(EPROTO);
#endif

#ifdef EPROTONOSUPPORT
    njs_errno_case(EPROTONOSUPPORT);
#endif

#ifdef EPROTOTYPE
    njs_errno_case(EPROTOTYPE);
#endif

#ifdef ERANGE
    njs_errno_case(ERANGE);
#endif

#ifdef EROFS
    njs_errno_case(EROFS);
#endif

#ifdef ESPIPE
    njs_errno_case(ESPIPE);
#endif

#ifdef ESRCH
    njs_errno_case(ESRCH);
#endif

#ifdef ESTALE
    njs_errno_case(ESTALE);
#endif

#ifdef ETIME
    njs_errno_case(ETIME);
#endif

#ifdef ETIMEDOUT
    njs_errno_case(ETIMEDOUT);
#endif

#ifdef ETXTBSY
    njs_errno_case(ETXTBSY);
#endif

#ifdef EXDEV
    njs_errno_case(EXDEV);
#endif

    default:
        break;
    }

    return "UNKNOWN CODE";
}


