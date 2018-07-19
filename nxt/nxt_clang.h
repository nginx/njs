
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NXT_CLANG_H_INCLUDED_
#define _NXT_CLANG_H_INCLUDED_


#include <stdarg.h>
#include <stddef.h>       /* offsetof(). */
#include <unistd.h>       /* NULL. */


#define nxt_inline         static inline __attribute__((always_inline))
#define nxt_noinline       __attribute__((noinline))
#define nxt_cdecl


#define nxt_container_of(p, type, field)                                      \
    (type *) ((u_char *) (p) - offsetof(type, field))

#define nxt_nitems(x)                                                         \
    (sizeof(x) / sizeof((x)[0]))



#if (NXT_HAVE_BUILTIN_EXPECT)
#define nxt_expect(c, x)   __builtin_expect((long) (x), (c))
#define nxt_fast_path(x)   nxt_expect(1, x)
#define nxt_slow_path(x)   nxt_expect(0, x)

#else
#define nxt_expect(c, x)   (x)
#define nxt_fast_path(x)   (x)
#define nxt_slow_path(x)   (x)
#endif


#if (NXT_HAVE_BUILTIN_UNREACHABLE)
#define nxt_unreachable()  __builtin_unreachable()

#else
#define nxt_unreachable()
#endif


#if (NXT_HAVE_BUILTIN_PREFETCH)
#define nxt_prefetch(a)    __builtin_prefetch(a)

#else
#define nxt_prefetch(a)
#endif


#if (NXT_HAVE_BUILTIN_CLZ)
#define nxt_leading_zeros(x)  (((x) == 0) ? 32 : __builtin_clz(x))

#else

nxt_inline uint32_t
nxt_leading_zeros(uint32_t x)
{
    uint32_t  n;

    /*
     * There is no sense to optimize this function, since almost
     * all platforms nowadays support the built-in instruction.
     */

    if (x == 0) {
        return 32;
    }

    n = 0;

    while ((x & 0x80000000) == 0) {
        n++;
        x <<= 1;
    }

    return n;
}

#endif


#if (NXT_HAVE_BUILTIN_CLZLL)
#define nxt_leading_zeros64(x)  (((x) == 0) ? 64 : __builtin_clzll(x))

#else

nxt_inline uint64_t
nxt_leading_zeros64(uint64_t x)
{
    uint64_t  n;

    /*
     * There is no sense to optimize this function, since almost
     * all platforms nowadays support the built-in instruction.
     */

    if (x == 0) {
        return 64;
    }

    n = 0;

    while ((x & 0x8000000000000000) == 0) {
        n++;
        x <<= 1;
    }

    return n;
}

#endif


#if (NXT_HAVE_GCC_ATTRIBUTE_VISIBILITY)
#define NXT_EXPORT         __attribute__((visibility("default")))

#else
#define NXT_EXPORT
#endif


#if (NXT_HAVE_GCC_ATTRIBUTE_ALIGNED)
#define nxt_aligned(x)     __attribute__((aligned(x)))

#else
#define nxt_aligned(x)
#endif


#if (NXT_HAVE_GCC_ATTRIBUTE_MALLOC)
#define NXT_MALLOC_LIKE    __attribute__((__malloc__))

#else
#define NXT_MALLOC_LIKE
#endif


#if (NXT_CLANG)
/* Any __asm__ directive disables loop vectorization in GCC and Clang. */
#define nxt_pragma_loop_disable_vectorization  __asm__("")

#else
#define nxt_pragma_loop_disable_vectorization
#endif


#endif /* _NXT_CLANG_H_INCLUDED_ */
