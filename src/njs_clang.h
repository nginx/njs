
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_CLANG_H_INCLUDED_
#define _NJS_CLANG_H_INCLUDED_


#include <stdarg.h>
#include <stddef.h>       /* offsetof(). */
#include <unistd.h>       /* NULL. */


#define njs_inline         static inline __attribute__((always_inline))
#define njs_noinline       __attribute__((noinline))
#define njs_cdecl


#define njs_container_of(p, type, field)                                      \
    (type *) ((u_char *) (p) - offsetof(type, field))

#define njs_nitems(x)                                                         \
    (sizeof(x) / sizeof((x)[0]))


#if (NJS_HAVE_BUILTIN_EXPECT)
#define njs_expect(c, x)   __builtin_expect((long) (x), (c))
#define njs_fast_path(x)   njs_expect(1, x)
#define njs_slow_path(x)   njs_expect(0, x)

#else
#define njs_expect(c, x)   (x)
#define njs_fast_path(x)   (x)
#define njs_slow_path(x)   (x)
#endif


#if (NJS_HAVE_BUILTIN_UNREACHABLE)
#define njs_unreachable()  __builtin_unreachable()

#else
#define njs_unreachable()
#endif


#if (NJS_HAVE_BUILTIN_PREFETCH)
#define njs_prefetch(a)    __builtin_prefetch(a)

#else
#define njs_prefetch(a)
#endif


#if (NJS_HAVE_BUILTIN_CLZ)
#define njs_leading_zeros(x)  (((x) == 0) ? 32 : __builtin_clz(x))

#else

njs_inline uint32_t
njs_leading_zeros(uint32_t x)
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


#if (NJS_HAVE_BUILTIN_CLZLL)
#define njs_leading_zeros64(x)  (((x) == 0) ? 64 : __builtin_clzll(x))

#else

njs_inline uint64_t
njs_leading_zeros64(uint64_t x)
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


#if (NJS_HAVE_GCC_ATTRIBUTE_VISIBILITY)
#define NJS_EXPORT         __attribute__((visibility("default")))

#else
#define NJS_EXPORT
#endif


#if (NJS_HAVE_GCC_ATTRIBUTE_ALIGNED)
#define njs_aligned(x)     __attribute__((aligned(x)))

#else
#define njs_aligned(x)
#endif


#if (NJS_HAVE_GCC_ATTRIBUTE_MALLOC)
#define NJS_MALLOC_LIKE    __attribute__((__malloc__))

#else
#define NJS_MALLOC_LIKE
#endif


#if (NJS_CLANG)
/* Any __asm__ directive disables loop vectorization in GCC and Clang. */
#define njs_pragma_loop_disable_vectorization  __asm__("")

#else
#define njs_pragma_loop_disable_vectorization
#endif


#if (NJS_HAVE_MEMORY_SANITIZER)
#include <sanitizer/msan_interface.h>

#define njs_msan_unpoison(ptr, size)  __msan_unpoison(ptr, size)
#endif


#endif /* _NJS_CLANG_H_INCLUDED_ */
