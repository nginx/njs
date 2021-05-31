
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#ifndef _NJS_CLANG_H_INCLUDED_
#define _NJS_CLANG_H_INCLUDED_


#include <stdarg.h>
#include <stddef.h>       /* offsetof(). */


#define njs_inline         static inline __attribute__((always_inline))
#define njs_noinline       __attribute__((noinline))
#define njs_cdecl


#define njs_container_of(p, type, field)                                      \
    (type *) ((u_char *) (p) - offsetof(type, field))

#define njs_nitems(x)                                                         \
    (sizeof(x) / sizeof((x)[0]))

#define njs_max(val1, val2)                                                   \
    ((val1 < val2) ? (val2) : (val1))

#define njs_min(val1, val2)                                                   \
    ((val1 < val2) ? (val1) : (val2))


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


#if (NJS_HAVE_GCC_ATTRIBUTE_PACKED)
#define NJS_PACKED    __attribute__((packed))

#else
#define NJS_PACKED
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


#define njs_stringify(v)    #v


#if (NJS_HAVE_MEMORY_SANITIZER)
#include <sanitizer/msan_interface.h>

#define njs_msan_unpoison(ptr, size)  __msan_unpoison(ptr, size)
#else

#define njs_msan_unpoison(ptr, size)
#endif


#if (NJS_HAVE_DENORMALS_CONTROL)
#include <xmmintrin.h>

/*
 * 0x8000 Flush to zero
 * 0x0040 Denormals are zeros
 */

#define NJS_MM_DENORMALS_MASK 0x8040

#define njs_mm_denormals(on)                                                  \
    _mm_setcsr((_mm_getcsr() & ~NJS_MM_DENORMALS_MASK) | (!(on) ? 0x8040: 0x0))
#else

#define njs_mm_denormals(on)
#endif


#ifndef NJS_MAX_ALIGNMENT

#if (NJS_SOLARIS)
/* x86_64: 16, i386: 4, sparcv9: 16, sparcv8: 8. */
#define NJS_MAX_ALIGNMENT  _MAX_ALIGNMENT

#elif (NJS_WINDOWS)
/* Win64: 16, Win32: 8. */
#define NJS_MAX_ALIGNMENT  MEMORY_ALLOCATION_ALIGNMENT

#elif (__amd64__)
#define NJS_MAX_ALIGNMENT  16

#elif (__i386__ || __i386)
#define NJS_MAX_ALIGNMENT  4

#elif (__arm__)
#define NJS_MAX_ALIGNMENT  16

#else
#define NJS_MAX_ALIGNMENT  16
#endif

#endif


#define njs_align_size(size, a)                                               \
    (((size) + ((size_t) (a) - 1)) & ~((size_t) (a) - 1))


#define njs_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) (a) - 1))                     \
                 & ~((uintptr_t) (a) - 1))

#define njs_trunc_ptr(p, a)                                                   \
    (u_char *) ((uintptr_t) (p) & ~((uintptr_t) (a) - 1))


#endif /* _NJS_CLANG_H_INCLUDED_ */
